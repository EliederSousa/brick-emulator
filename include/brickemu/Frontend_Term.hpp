#pragma once

// Terminal frontend entry point (Linux; zero dependencies beyond libc).
// Same mechanics as another_source/brickgame-4bit: termios raw mode,
// ANSI cursor addressing, differential row redraw, non-blocking keyboard.
// Rendering differs: instead of a hand-authored layout it rasterizes the
// baked display-only segment map via Apollo18Display, downsampled to a text
// grid (default 21x60 with 16x8 LCD pixels per cell, matching terminal
// character aspect; --cell/--cell-w/--cell-h override).

#include "brickemu/Config.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace brickemu {

struct TermOptions {
    bool interactive = true; // false with --shot: dump one frame, exit
    int shotFrame = 0;       // with --shot N: print frame N as plain text
    std::string script;      // comma list: on,start,left,right,down,rotate,
                             // mute,reset (each = press, 30 frames, release)
    int fps = 30;            // present rate in interactive mode
    int holdMs = 200;        // release delay after the last key repeat
    // Text-cell geometry in LCD pixels. Terminal characters are ~twice as
    // tall as wide, so the default cell is 16x8: grid = 332/16 x 480/8
    // (21x60) with undistorted proportions. --cell N sets square NxN
    // blocks (exact, used for automated frame comparison).
    int cellW = 16;
    int cellH = 8;
    bool playfield = false;  // --pf: render only the 10x17 playfield matrix
                             // as single 'x'/' ' instead of full LCD raster
    bool noBorder = false;   // --no-border: skip the ╔═╗║╚═╝ outline
};

int runTerm(const BrickConfig& config, const TermOptions& options);

// Apollo 18 in 1 button map (values from the Python Apollo18in1B0302.brick
// direct_input): button name -> (port, mask). Shared with unit tests.
struct ButtonMap {
    const char* name;
    const char* port;
    std::uint8_t mask;
};

// Held-key tracker for terminal input (terminals report no key-up events;
// auto-repeat characters keep refreshing the release deadline so a held
// key stays continuously driven, e.g. racing-game steering).
class HeldKeys {
public:
    using Clock = std::chrono::steady_clock;

    // Record a press (or repeat): the button stays held until hold elapses
    // with no further repeat. Repeated presses extend, never stack.
    void press(const ButtonMap* button, Clock::time_point now,
               std::chrono::milliseconds hold) {
        for (auto& entry : held_) {
            if (entry.button == button) {
                entry.releaseAt = now + hold;
                return;
            }
        }
        held_.push_back({button, now + hold});
    }

    // Pop buttons whose deadline passed without a repeat.
    std::vector<const ButtonMap*> expired(Clock::time_point now) {
        std::vector<const ButtonMap*> out;
        for (auto it = held_.begin(); it != held_.end();) {
            if (now >= it->releaseAt) {
                out.push_back(it->button);
                it = held_.erase(it);
            } else {
                ++it;
            }
        }
        return out;
    }

    std::size_t size() const { return held_.size(); }

private:
    struct Entry {
        const ButtonMap* button;
        Clock::time_point releaseAt;
    };
    std::vector<Entry> held_;
};

} // namespace brickemu
