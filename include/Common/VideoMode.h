// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef VIDEOMODE_H_INCLUDED
#define VIDEOMODE_H_INCLUDED

#include <cstdint>

static constexpr uint8_t SPRITE_STEAL_POINTER = 1u << 1;
static constexpr uint8_t SPRITE_STEAL_DATA0   = 1u << 2;
static constexpr uint8_t SPRITE_STEAL_DATA1   = 1u << 3;
static constexpr uint8_t SPRITE_STEAL_DATA2   = 1u << 4;

// Video mode
enum class VideoMode { NTSC, PAL};

struct ModeConfig
{
    uint16_t maxRasterLines;
    uint8_t  cyclesPerLine;
    uint8_t  frameRate;
    uint16_t vblankStartLine;
    uint16_t vblankEndLine;
    int      visibleLines;
    int      badLineCycles;
    int      firstVisibleLine;
    int      lastVisibleLine;
    int      DMAStartCycle;
    int      DMAEndCycle;
    int      hardware_X;
    int      bgFetchStartCycle;
    int      bgFetchEndCycle;
    int      refreshStartCycle;
    int      spriteDmaCheckCycle1;
    int      spriteDmaCheckCycle2;
    int      spriteYExpansionToggleCycle;
    int      spriteMcBaseAdvanceCycle1;
    int      spriteMcBaseAdvanceCycle2;

    struct SpriteFetchTiming
    {
        int pointerCycle;
        int data0Cycle;
        int data1Cycle;
        int data2Cycle;
    };

    SpriteFetchTiming spriteFetchTiming[8];

    uint8_t  spriteCpuStealPhaseMask;
};

inline constexpr ModeConfig NTSC_CONFIG =
{
    263,   // maxRasterLines - 6567R8
    65,    // cyclesPerLine
    60,    // frameRate
    251,   // vblankStartLine
    21,    // vblankEndLine
    200,   // visibleLines
    40,    // badLineCycles
    51,    // firstVisibleLine
    250,   // lastVisibleLine
    15,    // DMAStartCycle
    54,    // DMAEndCycle
    24,    // hardware_X
    15,    // bgFetchStartCycle
    54,    // bgFetchEndCycle
    10,    // refreshStartCycle

    55,    // spriteDmaCheckCycle1
    56,    // spriteDmaCheckCycle2
    54,    // spriteYExpansionToggleCycle
    14,    // spriteMcBaseAdvanceCycle1
    15,    // spriteMcBaseAdvanceCycle2

    {
        {55, 56, 57, 58},
        {58, 59, 60, 61},
        {61, 62, 63, 64},
        {64,  0,  1,  2},
        { 2,  3,  4,  5},
        { 5,  6,  7,  8},
        { 8,  9, 10, 11},
        {11, 12, 13, 14}
    }, // spriteFetchTiming

    SPRITE_STEAL_DATA0 | SPRITE_STEAL_DATA2
};

inline constexpr ModeConfig PAL_CONFIG =
{
    312,   // maxRasterLines
    63,    // cyclesPerLine
    50,    // frameRate
    251,   // vblankStartLine
    50,    // vblankEndLine
    200,   // visibleLines
    40,    // badLineCycles
    51,    // firstVisibleLine
    250,   // lastVisibleLine
    14,    // DMAStartCycle
    53,    // DMAEndCycle
    24,    // hardware_X
    14,    // bgFetchStartCycle
    53,    // bgFetchEndCycle
    10,    // refreshStartCycle

    54,    // spriteDmaCheckCycle1
    55,    // spriteDmaCheckCycle2
    54,    // spriteYExpansionToggleCycle
    14,    // spriteMcBaseAdvanceCycle1
    15,    // spriteMcBaseAdvanceCycle2

    {
        {54, 55, 56, 57},
        {57, 58, 59, 60},
        {60, 61, 62,  0},
        { 0,  1,  2,  3},
        { 3,  4,  5,  6},
        { 6,  7,  8,  9},
        { 9, 10, 11, 12},
        {12, 13, 14, 15}
    }, // spriteFetchTiming

    SPRITE_STEAL_DATA0 | SPRITE_STEAL_DATA2
};
#endif // VIDEOMODE_H_INCLUDED
