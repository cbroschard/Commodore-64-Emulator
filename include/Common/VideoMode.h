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
    int      spriteFetchSlots[8];
};

inline constexpr ModeConfig NTSC_CONFIG =
{
    262,   // maxRasterLines
    65,   // cyclesPerLine
    60,   // frameRate
    251,   // vblankStartLine
    21,   // vblankEndLine
    200,   // visibleLines
    40,   // badLineCycles
    51,   // firstVisibleLine
    250,   // lastVisibleLine
    15,    // DMAStartCycle
    54,     // DMAEndCycle
    24,     // hardwareX
    15,    // bgFetchStartCycle
    54,     // bgFetchEndCycle
    55,     // refreshStartCycle
    {55,58,61,64,2,5,8,11} // spriteFetchCycle
};

inline constexpr ModeConfig PAL_CONFIG =
{
    312,   // maxRasterLines
    63,   // cyclesPerLine
    50,   // frameRate
    251,   // vblankStartLine
    50,   // vblankEndLine
    200,   // visibleLines
    40,   // badLineCycles
    51,   // firstVisibleLine
    250,   // lastVisibleLine
    14,    // DMAStartCycle
    53,     // DMAEndCycle
    31,      // hardwareX
    15,    // bgFetchStartCycle
    54,     // bgFetchEndCycle
    54,     // refreshStartCycle
    {54,57,60,0,3,6,9,12} // spriteFetchCycle
};

#endif // VIDEOMODE_H_INCLUDED
