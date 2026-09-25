// SDL2 frontend: real-time loop for handhelds (RG35XX H class, 1 GB RAM).
//
// Efficiency choices (this whole port targets weak ARM SoCs):
// - Display-only LCD canvas (332x480) in an INDEX8 streaming texture with a
//   2-entry LCD palette: per-frame cost is one ~155 KB upload, and only when
//   the 48-byte LCDRAM actually changed (memcmp inside Apollo18Display).
// - SDL_Renderer default (accelerated where the firmware provides GLES,
//   software fallback otherwise); SDL_RenderSetLogicalSize letterboxes the
//   portrait LCD onto the 640x480 landscape screen, no manual scaling.
// - Single thread, static buffers, no per-frame allocation.
// - Software audio mixing comes in the next slice (SPL0Xsound port).

#include "brickemu/Frontend_SDL.hpp"

#include "brickemu/CoreRegistry.hpp"
#include "brickemu/DisplayBuffer.hpp"
#include "brickemu/SPL03.hpp"

#include <SDL.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <random>
#include <string>
#include <tuple>
#include <vector>

namespace brickemu {

static std::uint16_t s_pngDisplay[Apollo18Display::kPixels];
static bool s_usePng = false;
static float s_ghostAlpha = 0.08f;
static SdlPngOptions s_pngOpt{38, 48, 1003, 1452, 0.f, 0.f, 0.3306f, 0.3306f, -1.f, false};
void setSdlPngOptions(const SdlPngOptions& opt) {
    // Fix as default 38,48,1003,1452 scale 0.3306 per user perfect config
    // Only override where user explicitly set
    if (opt.cropX >= 0) s_pngOpt.cropX = opt.cropX;
    if (opt.cropY >= 0) s_pngOpt.cropY = opt.cropY;
    if (opt.cropW > 0) s_pngOpt.cropW = opt.cropW;
    if (opt.cropH > 0) s_pngOpt.cropH = opt.cropH;
    if (opt.offsetX != 0.f || opt.offsetY != 0.f) { s_pngOpt.offsetX = opt.offsetX; s_pngOpt.offsetY = opt.offsetY; }
    if (opt.scaleX > 0) s_pngOpt.scaleX = opt.scaleX;
    if (opt.scaleY > 0) s_pngOpt.scaleY = opt.scaleY;
    if (opt.ghost >= 0) { s_pngOpt.ghost = opt.ghost; s_ghostAlpha = opt.ghost; }
    if (opt.debug) s_pngOpt.debug = true;
}

namespace {

// Display-only LCD canvas (332x480) in an RGB565 streaming texture.
// (INDEX8 textures have no public palette API in SDL2, so the 1-byte index
// buffer from Apollo18Display is expanded to RGB565 on upload — still only
// on frames where the 48-byte LCDRAM changed.)
constexpr std::uint16_t kLcdBg =
    ((156 >> 3) << 11) | ((166 >> 2) << 5) | (108 >> 3);
constexpr std::uint16_t kLcdFg =
    ((30 >> 3) << 11) | ((34 >> 2) << 5) | (26 >> 3);

constexpr int kWindowW = 640;
constexpr int kWindowH = 480;

inline std::uint16_t blend565(std::uint16_t fg, std::uint16_t bg, float a) {
    int fr = (fg >> 11) & 0x1F, fg6 = (fg >> 5) & 0x3F, fb = fg & 0x1F;
    int br = (bg >> 11) & 0x1F, bg6 = (bg >> 5) & 0x3F, bb = bg & 0x1F;
    fr = (fr << 3) | (fr >> 2); fg6 = (fg6 << 2) | (fg6 >> 4); fb = (fb << 3) | (fb >> 2);
    br = (br << 3) | (br >> 2); bg6 = (bg6 << 2) | (bg6 >> 4); bb = (bb << 3) | (bb >> 2);
    int r = int(br * (1 - a) + fr * a);
    int g = int(bg6 * (1 - a) + fg6 * a);
    int b = int(bb * (1 - a) + fb * a);
    return static_cast<std::uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

// SDL audio (SPL0Xsound port 4ch) - minimal port of audio_engine.py
constexpr int kSampleRate = 44100;
constexpr int kSamplesPerFrame = kSampleRate / 60;
static SDL_AudioDeviceID s_audioDev = 0;
static double s_phaseA = 0, s_phaseN = 0;
static std::mt19937 s_rng{42};
static float s_volA = 0, s_freqA = 0;
static float s_volN = 0, s_freqN = 0;
// speech queue (channel 3) sample-accurate, like Python Channel tone_queue
static std::vector<std::pair<int,float>> s_speechQueue; // (samplePos, amp)
static std::vector<std::tuple<int,float,float>> s_toneAQueue; // pos,freq,amp
static std::vector<std::tuple<int,float,float>> s_noiseQueue; // pos,freq,amp
static int s_curSample = 0;
static float s_curSpeechAmp = 0;
static float s_curToneAFreq = 0, s_curToneAAmp = 0;
static float s_curNoiseFreq = 0, s_curNoiseAmp = 0;

static void handleAudioEmit(int ch, float freq, bool noise, float amp, double t) {
    int pos = 4096 + int(kSampleRate * t);
    if (pos < s_curSample) pos = s_curSample;
    if (ch == 0) {
        s_toneAQueue.emplace_back(pos, freq, amp);
        if (s_toneAQueue.size() > 1000) s_toneAQueue.erase(s_toneAQueue.begin());
    } else if (ch == 2) {
        s_noiseQueue.emplace_back(pos, freq, amp);
        if (s_noiseQueue.size() > 1000) s_noiseQueue.erase(s_noiseQueue.begin());
    } else if (ch == 3) {
        s_speechQueue.emplace_back(pos, amp);
        if (s_speechQueue.size() > 1000) s_speechQueue.erase(s_speechQueue.begin());
    }
}

static void initAudio() {
    SDL_AudioSpec want{}, have{};
    want.freq = kSampleRate;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 4096;
    s_audioDev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (s_audioDev) SDL_PauseAudioDevice(s_audioDev, 0);
    else std::fprintf(stderr, "SDL audio failed: %s\n", SDL_GetError());
    SPL03::setAudioCallback(handleAudioEmit);
}
static void updateAudioFromCpu(ICpuCore* cpu) {
    if (!s_audioDev) return;
    (void)cpu; // queues filled via handleAudioEmit per-write, no polling
    const int N = kSamplesPerFrame;
    std::vector<int16_t> buf(N);
    const double volScale = 655; // SOUND_LEVEL = 32767*0.02 Python
    for (int i = 0; i < N; ++i) {
        int gpos = s_curSample + i;
        while (!s_toneAQueue.empty() && std::get<0>(s_toneAQueue.front()) <= gpos) {
            s_curToneAFreq = std::get<1>(s_toneAQueue.front());
            s_curToneAAmp = std::get<2>(s_toneAQueue.front());
            s_toneAQueue.erase(s_toneAQueue.begin());
        }
        while (!s_noiseQueue.empty() && std::get<0>(s_noiseQueue.front()) <= gpos) {
            s_curNoiseFreq = std::get<1>(s_noiseQueue.front());
            s_curNoiseAmp = std::get<2>(s_noiseQueue.front());
            s_noiseQueue.erase(s_noiseQueue.begin());
        }
        while (!s_speechQueue.empty() && s_speechQueue.front().first <= gpos) {
            s_curSpeechAmp = s_speechQueue.front().second;
            s_speechQueue.erase(s_speechQueue.begin());
        }
        double s = 0;
        if (s_curToneAAmp > 0 && s_curToneAFreq > 0) {
            s += std::sin(s_phaseA) * s_curToneAAmp;
            s_phaseA += 2 * M_PI * s_curToneAFreq / kSampleRate;
            if (s_phaseA > 2*M_PI) s_phaseA -= 2*M_PI;
        }
        if (s_curNoiseAmp > 0 && s_curNoiseFreq > 0) {
            double ns = std::sin(s_phaseN) * s_curNoiseAmp;
            s_phaseN += 2 * M_PI * s_curNoiseFreq / kSampleRate;
            if (s_phaseN > 2*M_PI) s_phaseN -= 2*M_PI;
            static double prev = 0; static int randv = 1;
            if (ns * prev < 0) { randv = (s_rng() & 1) ? 1 : -1; prev = ns; }
            s += ns * randv;
        }
        s += s_curSpeechAmp * 0.8;
        int v = int(s * volScale);
        if (v > 655) v = 655; if (v < -655) v = -655;
        v = v * 50;
        if (v > 32767) v = 32767; if (v < -32767) v = -32767;
        buf[i] = int16_t(v);
    }
    s_curSample += N;
    SDL_QueueAudio(s_audioDev, buf.data(), buf.size()*sizeof(int16_t));
}

static bool loadPngDisplayCropped(const char* path) {
    int w, h, comp;
    unsigned char* data = stbi_load(path, &w, &h, &comp, 4);
    if (!data) {
        std::fprintf(stderr, "PNG load failed %s: %s\n", path, stbi_failure_reason());
        return false;
    }
    // Auto bbox (non-transparent) unless overridden via --png-crop / --png-offset
    int minX = w, minY = h, maxX = -1, maxY = -1;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            unsigned char a = data[(y * w + x) * 4 + 3];
            if (a > 10) {
                if (x < minX) minX = x;
                if (y < minY) minY = y;
                if (x > maxX) maxX = x;
                if (y > maxY) maxY = y;
            }
        }
    }
    if (maxX < 0) { stbi_image_free(data); return false; }
    if (s_pngOpt.cropX >= 0) minX = s_pngOpt.cropX;
    if (s_pngOpt.cropY >= 0) minY = s_pngOpt.cropY;
    if (s_pngOpt.cropW > 0) { maxX = minX + s_pngOpt.cropW - 1; }
    if (s_pngOpt.cropH > 0) { maxY = minY + s_pngOpt.cropH - 1; }
    int cw = maxX - minX + 1;
    int ch = maxY - minY + 1;
    float scaleX = (s_pngOpt.scaleX > 0 ? s_pngOpt.scaleX : (float)Apollo18Display::kWidth / cw);
    float scaleY = (s_pngOpt.scaleY > 0 ? s_pngOpt.scaleY : (float)Apollo18Display::kHeight / ch);
    // Use separate X/Y scales to fix proportion drift (middle ok, corners off)
    for (int y = 0; y < Apollo18Display::kHeight; ++y) {
        int sy = minY + int((y - s_pngOpt.offsetY) / scaleY);
        for (int x = 0; x < Apollo18Display::kWidth; ++x) {
            int sx = minX + int((x - s_pngOpt.offsetX) / scaleX);
            if (sx < 0) sx = 0; if (sx >= w) sx = w - 1;
            if (sy < 0) sy = 0; if (sy >= h) sy = h - 1;
            unsigned char* p = data + (sy * w + sx) * 4;
            int r = p[0], g = p[1], b = p[2], a = p[3];
            if (a < 255) {
                r = (r * a + 156 * (255 - a)) / 255;
                g = (g * a + 166 * (255 - a)) / 255;
                b = (b * a + 108 * (255 - a)) / 255;
            }
            s_pngDisplay[y * Apollo18Display::kWidth + x] =
                static_cast<std::uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        }
    }
    if (s_pngOpt.debug) {
        // red border for crop bbox
        for (int x = 0; x < Apollo18Display::kWidth; ++x) { s_pngDisplay[x] = 0xF800; s_pngDisplay[(Apollo18Display::kHeight-1)*Apollo18Display::kWidth + x] = 0xF800; }
        for (int y = 0; y < Apollo18Display::kHeight; ++y) { s_pngDisplay[y*Apollo18Display::kWidth] = 0xF800; s_pngDisplay[y*Apollo18Display::kWidth + Apollo18Display::kWidth-1] = 0xF800; }
    }
    stbi_image_free(data);
    std::fprintf(stderr, "PNG cropped %dx%d+%d+%d scale %.4fx%.4f offset %.1f,%.1f ghost %.2f%s\n",
        cw, ch, minX, minY, scaleX, scaleY, s_pngOpt.offsetX, s_pngOpt.offsetY, s_ghostAlpha, s_pngOpt.debug?" debug":"");
    return true;
}

static bool tryLoadPngDisplay() {
    const char* ghostEnv = std::getenv("BRICKEMU_GHOST");
    if (ghostEnv) {
        float v = std::atof(ghostEnv);
        if (v >= 0.f && v <= 1.f) s_ghostAlpha = v;
    }
    const char* cands[] = {"./Apollo18in1display.png", "assets/Apollo18in1display.png","../assets/Apollo18in1display.png","./portmaster-build/assets/Apollo18in1display.png",nullptr};
    for (int i = 0; cands[i]; ++i) if (loadPngDisplayCropped(cands[i])) {
        std::fprintf(stderr, "PNG brick texture loaded: %s ghost=%.2f\n", cands[i], s_ghostAlpha);
        return true;
    }
    return false;
}

// Apollo 18 in 1 button map (values from the Python Apollo18in1B0302.brick
// direct_input): button name -> (port, mask). Press drives level 1,
// release clears; RES resets the core on press.
struct ButtonMap {
    const char* name;
    const char* port;
    std::uint8_t mask;
};
constexpr ButtonMap kButtons[] = {
    {"btnOnOff", "PA", 1},  {"btnMute", "PA", 32}, {"btnStartP", "PA", 64},
    {"btnReset", "RES", 0}, {"btnLeft", "PA", 8},  {"btnRight", "PA", 16},
    {"btnDown", "PA", 4},   {"btnRotate", "PA", 2},
};

const ButtonMap* findButton(const char* name) {
    for (const auto& b : kButtons) {
        if (std::string(b.name) == name) {
            return &b;
        }
    }
    return nullptr;
}

void driveButton(ICpuCore& cpu, const ButtonMap& b, bool pressed) {
    if (std::string(b.port) == "RES") {
        if (pressed) {
            cpu.setPortInput("RES", 0, 0);
        }
        return;
    }
    cpu.setPortInput(b.port, b.mask, pressed ? 1 : -1);
}

// Keyboard fallback (also what gptokeyb targets on device).
const ButtonMap* keyButton(SDL_Keycode key) {
    switch (key) {
    case SDLK_LEFT:
    case SDLK_a: return findButton("btnLeft");
    case SDLK_RIGHT:
    case SDLK_d: return findButton("btnRight");
    case SDLK_DOWN:
    case SDLK_s: return findButton("btnDown");
    case SDLK_UP:
    case SDLK_w:
    case SDLK_SPACE: return findButton("btnRotate");
    case SDLK_RETURN: return findButton("btnStartP");
    case SDLK_o: return findButton("btnOnOff");
    case SDLK_m: return findButton("btnMute");
    case SDLK_r: return findButton("btnReset");
    default: return nullptr;
    }
}

// Gamecontroller mapping for the brick layout.
const ButtonMap* padButton(Uint8 button) {
    switch (button) {
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return findButton("btnLeft");
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return findButton("btnRight");
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return findButton("btnDown");
    case SDL_CONTROLLER_BUTTON_A: return findButton("btnRotate");
    case SDL_CONTROLLER_BUTTON_START: return findButton("btnStartP");
    case SDL_CONTROLLER_BUTTON_BACK: return findButton("btnMute");
    case SDL_CONTROLLER_BUTTON_Y: return findButton("btnOnOff");
    case SDL_CONTROLLER_BUTTON_X: return findButton("btnReset");
    default: return nullptr;
    }
}

} // namespace

int runSdl(const BrickConfig& config) {
    std::string error;
    auto cpu = CoreRegistry::create(config, &error);
    if (!cpu) {
        std::fprintf(stderr, "failed to create core: %s\n", error.c_str());
        return 3;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 4;
    }
    initAudio();

    SDL_Window* window = SDL_CreateWindow(
        "BrickEmu", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        kWindowW, kWindowH, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window) {
        std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 4;
    }
    SDL_Renderer* renderer = SDL_CreateRenderer(
        window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) { // software fallback (KMSDRM without GL, etc.)
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!renderer) {
        std::fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 4;
    }
    SDL_RenderSetLogicalSize(renderer, Apollo18Display::kWidth,
                             Apollo18Display::kHeight);

    SDL_Texture* lcd = SDL_CreateTexture(
        renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING,
        Apollo18Display::kWidth, Apollo18Display::kHeight);
    if (!lcd) {
        std::fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 4;
    }
    // Static upload buffer (332*480*2 B ~ 311 KB, .bss, no per-frame alloc).
    static std::uint16_t s_upload[Apollo18Display::kPixels];

    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i)) {
            SDL_GameControllerOpen(i);
            break; // first pad is enough for a brick handheld
        }
    }

    Apollo18Display display;
    s_usePng = tryLoadPngDisplay();
    const std::uint32_t cyclesPerSec =
        config.clockHz != 0 ? config.clockHz : 690000U;
    const double cyclesPerFrame = (double)cyclesPerSec / 60.0;
    // Persistent debt: total emulated cycles track the ideal 60 Hz timeline
    // within one instruction (no per-frame drift).
    double cycleDebt = 0.0;
    auto nextDeadline = std::chrono::steady_clock::now();

    const char* dumpPath = std::getenv("BRICKEMU_DUMPFRAME");
    const int dumpAt =
        std::getenv("BRICKEMU_DUMPFRAME_AT") ? std::atoi(std::getenv("BRICKEMU_DUMPFRAME_AT")) : 600;
    int frames = 0;
    bool running = true;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_QUIT: running = false; break;
            case SDL_KEYDOWN:
                if (ev.key.keysym.sym == SDLK_ESCAPE) {
                    running = false;
                } else if (const ButtonMap* b = keyButton(ev.key.keysym.sym)) {
                    driveButton(*cpu, *b, true);
                }
                break;
            case SDL_KEYUP:
                if (const ButtonMap* b = keyButton(ev.key.keysym.sym)) {
                    driveButton(*cpu, *b, false);
                }
                break;
            case SDL_CONTROLLERBUTTONDOWN:
                if (const ButtonMap* b = padButton(ev.cbutton.button)) {
                    driveButton(*cpu, *b, true);
                }
                break;
            case SDL_CONTROLLERBUTTONUP:
                if (const ButtonMap* b = padButton(ev.cbutton.button)) {
                    driveButton(*cpu, *b, false);
                }
                break;
            default: break;
            }
        }
        if (!running) {
            break;
        }

        // Fixed timestep: exactly one frame's worth of cycles per frame
        // (deterministic, constant speed on device; vsync paces present).
        cycleDebt += cyclesPerFrame;
        while (cycleDebt >= 1.0) {
            cycleDebt -= (double)cpu->clock();
        }
        updateAudioFromCpu(cpu.get());

        if (display.render(cpu->vram().data())) {
            const auto& px = display.pixels();
            if (s_usePng) {
                for (std::uint32_t i = 0; i < Apollo18Display::kPixels; ++i) {
                    if (px[i]) {
                        s_upload[i] = s_pngDisplay[i];
                    } else {
                        s_upload[i] = blend565(s_pngDisplay[i], kLcdBg, s_ghostAlpha);
                    }
                }
            } else {
                for (std::uint32_t i = 0; i < Apollo18Display::kPixels; ++i) {
                    s_upload[i] = px[i] ? kLcdFg : kLcdBg;
                }
            }
            SDL_Rect full{0, 0, Apollo18Display::kWidth,
                          Apollo18Display::kHeight};
            SDL_UpdateTexture(lcd, &full, s_upload,
                              Apollo18Display::kWidth * 2);
        }
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, lcd, nullptr, nullptr);
        SDL_RenderPresent(renderer);
        ++frames;

        // Without vsync (software fallback), pace to 60 Hz manually.
        nextDeadline += std::chrono::microseconds(1000000 / 60);
        Uint32 remaining = 0;
        {
            const auto now = std::chrono::steady_clock::now();
            if (nextDeadline > now) {
                remaining = (Uint32)std::chrono::duration_cast<std::chrono::milliseconds>(
                    nextDeadline - now).count();
            } else if (now > nextDeadline + std::chrono::milliseconds(250)) {
                nextDeadline = now;
            }
        }
        if (remaining > 0) {
            SDL_Delay(remaining);
        }

        if (dumpPath && frames >= dumpAt) {
            SDL_Surface* shot = SDL_CreateRGBSurfaceWithFormat(
                0, Apollo18Display::kWidth, Apollo18Display::kHeight, 24,
                SDL_PIXELFORMAT_RGB24);
            if (shot) {
                SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_RGB24,
                                     shot->pixels, shot->pitch);
                SDL_SaveBMP(shot, dumpPath);
                SDL_FreeSurface(shot);
            }
            running = false;
        }
    }

    if (s_audioDev) SDL_CloseAudioDevice(s_audioDev);
    SDL_DestroyTexture(lcd);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

} // namespace brickemu
