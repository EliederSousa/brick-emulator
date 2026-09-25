// Terminal frontend: playable Apollo 18 in 1 in any ANSI terminal.
//
// Follows another_source/brickgame-4bit conventions (termios raw mode,
// "\33[{row};{col}H" cursor addressing, differential redraw, WASD/arrows +
// space/enter, Esc quits), but the picture comes from the baked segment map
// (Apollo18Segments.hpp) via Apollo18Display, downsampled 8x8 LCD pixels per
// text cell ("@" on, "." off) into a 42x60 grid. Static buffers only:
// 155 KB index pixels + ~2.5 KB text grid. No SDL, no ncurses.

#include "brickemu/Frontend_Term.hpp"

#include "brickemu/Apollo18Hud.hpp"
#include "brickemu/CoreRegistry.hpp"
#include "brickemu/DisplayBuffer.hpp"

#include <termios.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace brickemu {
namespace {

constexpr int kDefaultCellW = 16; // 2:1 to match terminal char aspect
constexpr int kDefaultCellH = 8;

// Apollo button table (ButtonMap is shared via Frontend_Term.hpp).
constexpr ButtonMap kApollo18Buttons[] = {
    {"on", "PA", 1},     {"mute", "PA", 32}, {"start", "PA", 64},
    {"reset", "RES", 0}, {"left", "PA", 8},  {"right", "PA", 16},
    {"down", "PA", 4},   {"rotate", "PA", 2},
};

const ButtonMap* findButton(const std::string& name) {
    for (const auto& b : kApollo18Buttons) {
        if (name == b.name) {
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

const ButtonMap* keyButton(char c, bool& quit) {
    quit = false;
    switch (c) {
    case 'a': return findButton("left");
    case 'd': return findButton("right");
    case 's': return findButton("down");
    case 'w':
    case ' ': return findButton("rotate");
    case '\n':
    case '\r':
    case 'p': return findButton("start");
    case 'o': return findButton("on");
    case 'm': return findButton("mute");
    case 'r': return findButton("reset");
    case 0x1b: quit = true; return nullptr;
    default: return nullptr;
    }
}

// Downsample index pixels to the text grid (cols x rows, cellW x cellH LCD
// pixels per cell, any-on wins).
void rasterize(const std::array<std::uint8_t, Apollo18Display::kPixels>& px,
               std::vector<char>& grid, int cols, int rows, int cellW,
               int cellH) {
    constexpr std::uint16_t W = Apollo18Display::kWidth;
    constexpr std::uint16_t H = Apollo18Display::kHeight;
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            bool on = false;
            const int y0 = r * cellH, x0 = c * cellW;
            for (int y = y0; y < y0 + cellH && (unsigned)y < H && !on; ++y) {
                for (int x = x0; x < x0 + cellW && (unsigned)x < W; ++x) {
                    if (px[(unsigned)y * W + (unsigned)x]) {
                        on = true;
                        break;
                    }
                }
            }
            grid[(unsigned)r * (unsigned)cols + (unsigned)c] = on ? '@' : '.';
        }
    }
}

void printGrid(const std::vector<char>& grid, int cols, int rows,
               bool border) {
    if (border) {
        std::string edge("╔");
        for (int c = 0; c < cols; ++c) {
            edge += "═";
        }
        std::puts((edge + "╗").c_str());
    }
    for (int r = 0; r < rows; ++r) {
        if (border) {
            std::fputs("║", stdout);
        }
        std::fwrite(&grid[(unsigned)r * (unsigned)cols], 1, (unsigned)cols,
                    stdout);
        if (border) {
            std::puts("║");
        } else {
            std::fputc('\n', stdout);
        }
    }
    if (border) {
        std::string edge("╚");
        for (int c = 0; c < cols; ++c) {
            edge += "═";
        }
        std::puts((edge + "╝").c_str());
    }
}

// Static empty border for interactive mode (content rows stream into the
// interior via cursor addressing; sides are never overwritten).
void printBorderAt(int cols, int rows) {
    std::string edge("╔");
    for (int c = 0; c < cols; ++c) {
        edge += "═";
    }
    std::printf("\33[1;1H%s╗", edge.c_str());
    for (int r = 0; r < rows; ++r) {
        std::printf("\33[%d;1H║\33[%d;%dH║", r + 2, r + 2, cols + 2);
    }
    edge.assign("╚");
    for (int c = 0; c < cols; ++c) {
        edge += "═";
    }
    std::printf("\33[%d;1H%s╝", rows + 2, edge.c_str());
    std::fflush(stdout);
}

// One-line HUD summary for --pf mode (score, speed, level, sound state).
std::string hudLine(const Apollo18Hud& hud) {
    char score[6];
    for (int i = 0; i < 5; ++i) {
        score[i] = hud.d[i] < 0 ? ' ' : static_cast<char>('0' + hud.d[i]);
    }
    score[5] = 0;
    char speed = hud.speed < 0 ? ' ' : static_cast<char>('0' + hud.speed);
    char level = hud.level < 0 ? ' ' : static_cast<char>('0' + hud.level);
    char buf[96];
    std::snprintf(buf, sizeof buf, "SCORE %s SPEED %c LEVEL %c SOUND %s",
                  score, speed, level, hud.sound ? "ON" : "OFF");
    return buf;
}

// Playfield mode: 10x17 matrix, one LCDRAM bit per cell, single 'x' for
// set cells and single ' ' for empty ones.
void rasterizePf(const std::uint8_t* lcdram, std::vector<char>& grid) {
    for (std::uint32_t r = 0; r < kApollo18PfRows; ++r) {
        for (std::uint32_t c = 0; c < kApollo18PfCols; ++c) {
            const auto& cell = kApollo18Pf[r * kApollo18PfCols + c];
            const bool on = ((lcdram[cell.byte] >> cell.bit) & 1U) != 0;
            grid[r * kApollo18PfCols + c] = on ? 'x' : ' ';
        }
    }
}

} // namespace

int runTerm(const BrickConfig& config, const TermOptions& options) {
    std::string error;
    auto cpu = CoreRegistry::create(config, &error);
    if (!cpu) {
        std::fprintf(stderr, "failed to create core: %s\n", error.c_str());
        return 3;
    }

    // --script "on,start,left,...": press, 30 frames, release, 10 frames.
    std::vector<std::string> script;
    {
        std::string cur;
        for (char c : options.script + ",") {
            if (c == ',') {
                if (!cur.empty()) {
                    script.push_back(cur);
                    cur.clear();
                }
            } else {
                cur += c;
            }
        }
    }

    Apollo18Display display;
    // Text grid geometry (--cell sets square blocks; default 16x8 matches
    // the ~1:2 terminal character aspect so shapes are undistorted).
    // --pf instead shows the 10x17 playfield matrix as [x]/[ ].
    const bool pfMode = options.playfield;
    const int cellW = options.cellW > 0 ? options.cellW : kDefaultCellW;
    const int cellH = options.cellH > 0 ? options.cellH : kDefaultCellH;
    const int cols = pfMode ? kApollo18PfCols
                            : (Apollo18Display::kWidth + cellW - 1) / cellW;
    const int rows = pfMode ? kApollo18PfRows
                            : (Apollo18Display::kHeight + cellH - 1) / cellH;
    std::vector<char> grid((unsigned)cols * (unsigned)rows, '.');
    std::vector<char> shown((unsigned)cols * (unsigned)rows, 0);
    std::array<std::uint8_t, kApollo18LcdramSize> lastPf{};
    bool pfFirst = true;

    const std::uint32_t cyclesPerSec =
        config.clockHz != 0 ? config.clockHz : 690000U;
    const double cyclesPerFrame = (double)cyclesPerSec / options.fps;
    // Persistent debt (carried across frames): total emulated cycles track
    // the ideal timeline within one instruction, so scripted/timed input
    // stays bit-exact with the reference instead of drifting per frame.
    double cycleDebt = 0.0;
    auto nextDeadline = std::chrono::steady_clock::now();

    struct termios saved{};
    bool rawMode = false;
    if (options.interactive && isatty(STDIN_FILENO)) {
        tcgetattr(STDIN_FILENO, &saved);
        struct termios raw = saved;
        raw.c_lflag &= (unsigned)~(ICANON | ECHO);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
        rawMode = true;
        std::printf("\33[2J\33[?25l\33[H"); // clear, hide cursor, home
        if (!options.noBorder) {
            printBorderAt(cols, rows);
        }
        std::fflush(stdout);
    }

    HeldKeys held;
    const std::chrono::milliseconds holdMs(
        options.holdMs > 0 ? options.holdMs : 200);
    size_t scriptPos = 0;
    int scriptGap = 0;
    bool scriptPressPending = true;
    int frame = 0;
    int shotCountdown = options.interactive ? -1 : options.shotFrame;
    bool running = true;
    while (running) {
        // Non-interactive exit points are checked BEFORE emulating so the
        // dumped frame accounts exactly the requested frames (no off-by-one:
        // --shot N dumps after precisely N frames, a finished script dumps
        // right at completion).
        if (!options.interactive) {
            const bool scriptDone =
                scriptPos >= script.size() && scriptGap <= 0;
            if (shotCountdown == 0 ||
                (scriptDone && shotCountdown < 0 && frame > 0)) {
                // --shot 0 (or an early dump) precedes any render pass:
                // rasterize the current state once so the dump is never
                // the '.'-initialized grid.
                if (pfMode && pfFirst) {
                    const auto ram = cpu->vram();
                    std::memcpy(lastPf.data(), ram.data(), lastPf.size());
                    pfFirst = false;
                    rasterizePf(ram.data(), grid);
                } else if (!pfMode && display.render(cpu->vram().data())) {
                    rasterize(display.pixels(), grid, cols, rows, cellW,
                              cellH);
                }
                printGrid(grid, cols, rows, !options.noBorder);
                if (pfMode) {
                    std::puts(hudLine(decodeHud(cpu->vram().data())).c_str());
                }
                break;
            }
        }
        // Scripted input (works headless too).
        if (scriptPos < script.size() && scriptGap <= 0) {
            if (const ButtonMap* b = findButton(script[scriptPos])) {
                if (scriptPressPending) {
                    driveButton(*cpu, *b, true);
                    scriptPressPending = false;
                    scriptGap = 30;
                } else {
                    driveButton(*cpu, *b, false);
                    scriptPressPending = true;
                    scriptGap = 10;
                    ++scriptPos;
                }
            } else {
                ++scriptPos; // unknown token: skip
            }
        }

        // Keyboard (interactive TTY only).
        if (rawMode) {
            char buf[16];
            const ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
            int esc = 0;
            for (ssize_t i = 0; i < n; ++i) {
                const char c = buf[i];
                if (c == 0x1b) {
                    esc = 1;
                } else if (c == 0x5b && esc == 1) {
                    esc = 2;
                } else if (esc == 2) {
                    const ButtonMap* b = nullptr;
                    if (c == 0x41) {
                        b = findButton("rotate"); // Up
                    } else if (c == 0x42) {
                        b = findButton("down");
                    } else if (c == 0x43) {
                        b = findButton("right");
                    } else if (c == 0x44) {
                        b = findButton("left");
                    }
                    esc = 0;
                    if (b) {
                        driveButton(*cpu, *b, true);
                        held.press(b, std::chrono::steady_clock::now(),
                                   holdMs);
                    }
                } else {
                    esc = 0;
                    bool quit = false;
                    if (const ButtonMap* b = keyButton(c, quit)) {
                        driveButton(*cpu, *b, true);
                        held.press(b, std::chrono::steady_clock::now(),
                                   holdMs);
                    } else if (quit) {
                        running = false;
                    }
                }
            }
            if (n != (ssize_t)sizeof(buf) && esc == 1) {
                running = false; // bare Escape quits
            }
            // Repeats refresh deadlines (see HeldKeys): a physically held key
            // stays driven until holdMs elapses with no new repeat character.
            for (const ButtonMap* b :
                 held.expired(std::chrono::steady_clock::now())) {
                driveButton(*cpu, *b, false);
            }
        }

        // Fixed timestep: exactly one frame's worth of cycles per
        // iteration (deterministic headless, constant speed on device).
        cycleDebt += cyclesPerFrame;
        while (cycleDebt >= 1.0) {
            cycleDebt -= (double)cpu->clock();
        }
        if (scriptGap > 0) {
            --scriptGap;
        }
        ++frame;

        // Render on LCDRAM change (full raster or playfield matrix).
        if (pfMode) {
            const auto ram = cpu->vram();
            if (pfFirst ||
                std::memcmp(ram.data(), lastPf.data(), lastPf.size()) != 0) {
                std::memcpy(lastPf.data(), ram.data(), lastPf.size());
                pfFirst = false;
                rasterizePf(ram.data(), grid);
            }
        } else if (display.render(cpu->vram().data())) {
            rasterize(display.pixels(), grid, cols, rows, cellW, cellH);
        }
        if (!options.interactive) {
            if (shotCountdown > 0) {
                --shotCountdown;
            }
            continue;
        }
        const int rowBase = options.noBorder ? 0 : 1;
        const int colBase = options.noBorder ? 1 : 2;
        for (int r = 0; r < rows; ++r) {
            if (std::memcmp(&grid[(unsigned)r * (unsigned)cols],
                            &shown[(unsigned)r * (unsigned)cols],
                            (unsigned)cols) != 0) {
                std::memcpy(&shown[(unsigned)r * (unsigned)cols],
                            &grid[(unsigned)r * (unsigned)cols],
                            (unsigned)cols);
                // Content rows sit inside the border (offset +1/+2).
                std::printf("\33[%d;%dH%.*s", r + 1 + rowBase, colBase, cols,
                            &grid[(unsigned)r * (unsigned)cols]);
            }
        }
        std::printf("\33[%d;1H[f%d pc=%04x] wasd/space/enter/o/m/r, esc=quit",
                    rows + 1 + rowBase * 2, frame, cpu->pc());
        if (pfMode) {
            std::printf(" %s",
                        hudLine(decodeHud(cpu->vram().data())).c_str());
        }
        std::fflush(stdout);
        if (options.interactive) {
            // Sleep until the next frame deadline (no catch-up sleep debt:
            // a slow frame just means running slightly slow, never a spiral).
            nextDeadline += std::chrono::microseconds(1000000 / options.fps);
            std::this_thread::sleep_until(nextDeadline);
            if (std::chrono::steady_clock::now() > nextDeadline +
                    std::chrono::milliseconds(250)) {
                nextDeadline = std::chrono::steady_clock::now();
            }
        } else {
            // Headless (shot/script): free-running, no sleeps.
            nextDeadline = std::chrono::steady_clock::now();
        }
    }

    if (rawMode) {
        tcsetattr(STDIN_FILENO, TCSANOW, &saved);
        std::printf("\33[m\33[2J\33[?25h\33[H"); // restore, show cursor
        std::fflush(stdout);
    }
    return 0;
}

} // namespace brickemu
