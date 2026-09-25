#pragma once

// SDL-free HUD decoder for Apollo 18 in 1: score / speed / level 7-seg
// digits plus icon bits, from LCDRAM via the baked tables in
// Apollo18Segments.hpp. Pure table lookups; static footprint ~zero.

#include "brickemu/Apollo18Segments.hpp"

#include <array>
#include <cstdint>

namespace brickemu {

struct Apollo18Hud {
    int d[5];      // score digits left to right (-1 = blank; d[0]: 1/blank)
    int speed;     // -1 = blank
    int level;     // -1 = blank
    bool sound;    // music-note icon
    bool dot;      // decimal point
    bool runner;
    bool alarm;    // either alarm segment
    bool coffee;
    bool speedLabel;
    bool levelLabel;
    std::uint8_t next[4]; // next-piece rows, bit (3 - col) set per lit cell
};

// Digit segment order: top, upper-left, upper-right, mid, lower-left,
// lower-right, bottom. Returns 0-9, or -1 for blank/undecodable.
inline int decodeSevenSeg(std::uint8_t mask) {
    switch (mask) {
    case 0: return -1;
    case 119: return 0; // T,UL,UR,LL,LR,B
    case 36: return 1;  // UR,LR
    case 93: return 2;  // T,UR,M,LL,B
    case 109: return 3; // T,UR,M,LR,B
    case 46: return 4;  // UL,UR,M,LR
    case 107: return 5; // T,UL,M,LR,B
    case 123: return 6; // T,UL,M,LL,LR,B
    case 37: return 7;  // T,UR,LR
    case 127: return 8; // all
    case 111: return 9; // T,UL,UR,M,LR,B
    default: return -1;
    }
}

inline std::uint8_t segBit(const std::uint8_t* lcdram, Apollo18SegBit s) {
    return (lcdram[s.byte] >> s.bit) & 1U;
}

inline int decodeDigit(const std::uint8_t* lcdram,
                       const std::array<Apollo18SegBit, 7>& digit) {
    std::uint8_t mask = 0;
    for (int i = 0; i < 7; ++i) {
        mask |= segBit(lcdram, digit[static_cast<std::size_t>(i)])
                << static_cast<unsigned>(i);
    }
    return decodeSevenSeg(mask);
}

inline Apollo18Hud decodeHud(const std::uint8_t* lcdram) {
    Apollo18Hud hud{};
    hud.d[0] = segBit(lcdram, kApollo18Lead1) ? 1 : -1;
    for (int i = 0; i < 4; ++i) {
        std::uint8_t mask = 0;
        for (int role = 0; role < 7; ++role) {
            mask |= segBit(lcdram,
                           kApollo18Score[static_cast<std::size_t>(i * 7 +
                                                                  role)])
                    << static_cast<unsigned>(role);
        }
        hud.d[i + 1] = decodeSevenSeg(mask);
    }
    hud.speed = decodeDigit(lcdram, kApollo18Speed);
    hud.level = decodeDigit(lcdram, kApollo18Level);
    hud.sound = segBit(lcdram, kApollo18_note) != 0;
    hud.dot = segBit(lcdram, kApollo18_dot) != 0;
    hud.runner = segBit(lcdram, kApollo18_runner) != 0;
    hud.alarm = segBit(lcdram, kApollo18Alarm[0]) != 0 ||
                segBit(lcdram, kApollo18Alarm[1]) != 0;
    hud.coffee = segBit(lcdram, kApollo18_coffee) != 0;
    hud.speedLabel = segBit(lcdram, kApollo18_speed_label) != 0;
    hud.levelLabel = segBit(lcdram, kApollo18_level_label) != 0;
    for (int r = 0; r < 4; ++r) {
        std::uint8_t row = 0;
        for (int c = 0; c < 4; ++c) {
            row |= segBit(
                       lcdram,
                       kApollo18Next[static_cast<std::size_t>(r * 4 + c)])
                   << static_cast<unsigned>(3 - c);
        }
        hud.next[static_cast<std::size_t>(r)] = row;
    }
    return hud;
}

} // namespace brickemu
