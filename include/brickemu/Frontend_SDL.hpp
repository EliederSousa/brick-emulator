#pragma once

// SDL2 frontend entry point (built only when SDL2 is available).
// Owns the real-time loop: pace the CPU at config.clockHz, push VRAM into
// an INDEX8 LCD texture at 60 Hz (only when LCDRAM changed), and map
// keyboard + gamecontroller input onto the core's PA-port buttons.

#include "brickemu/Config.hpp"

namespace brickemu {

struct SdlPngOptions {
    int cropX = -1, cropY = -1, cropW = -1, cropH = -1; // override auto trim
    float offsetX = 0.f, offsetY = 0.f; // canvas offset in px
    float scaleX = -1.f, scaleY = -1.f; // override scale, -1 = auto
    float ghost = -1.f; // -1 = use env/default 0.08
    bool debug = false; // draw red crop border
};
void setSdlPngOptions(const SdlPngOptions& opt);

// Runs until SDL_QUIT/Escape, or until BRICKEMU_DUMPFRAME_AT frames have
// been presented when BRICKEMU_DUMPFRAME is set (headless frame check).
// Returns 0 on success.
int runSdl(const BrickConfig& config);

} // namespace brickemu
