#pragma once

// SDL-free LCD rasterizer for Apollo 18 in 1.
//
// Turns the 48-byte LCDRAM into an 8-bit index pixel buffer (0 = glass,
// 1 = segment on) using the baked display-only segment map
// (Apollo18Segments.hpp, generated offline from the SVG face).
// The SDL2 frontend uploads this buffer into an INDEX8 texture with an
// LCD palette, so per-frame cost is one 155 KB streaming upload, and only
// when LCDRAM actually changed (tracked internally, 48-byte memcmp).
// Static footprint: ~155 KB pixels + 48 B shadow. No heap, no threads.

#include "brickemu/Apollo18Segments.hpp"

#include <array>
#include <cstdint>
#include <cstring>

namespace brickemu {

class Apollo18Display {
public:
    static constexpr std::uint16_t kWidth = kApollo18CanvasW;   // 332
    static constexpr std::uint16_t kHeight = kApollo18CanvasH;  // 480
    static constexpr std::uint32_t kPixels = kWidth * kHeight;

    Apollo18Display() { pixels_.fill(0); lastRam_.fill(0); }

    // Re-rasterize from LCDRAM. Returns true when the picture changed.
    bool render(const std::uint8_t* lcdram) {
        if (!first_ && std::memcmp(lcdram, lastRam_.data(), lastRam_.size()) == 0) {
            return false;
        }
        first_ = false;
        std::memcpy(lastRam_.data(), lcdram, lastRam_.size());
        pixels_.fill(0);
        for (const auto& s : kApollo18Segs) {
            if (s.byte >= lastRam_.size()) {
                continue;
            }
            if ((lastRam_[s.byte] >> s.bit) & 1U) {
                for (std::uint16_t r = 0; r < s.h; ++r) {
                    const std::uint32_t base =
                        static_cast<std::uint32_t>(s.y + r) * kWidth + s.x;
                    std::memset(&pixels_[base], 1, s.w);
                }
            }
        }
        return true;
    }

    const std::array<std::uint8_t, kPixels>& pixels() const { return pixels_; }

private:
    std::array<std::uint8_t, kPixels> pixels_;
    std::array<std::uint8_t, kApollo18LcdramSize> lastRam_;
    bool first_ = true;
};

} // namespace brickemu
