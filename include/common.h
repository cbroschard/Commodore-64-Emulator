// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef COMMON_H_INCLUDED
#define COMMON_H_INCLUDED

#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>
#include <vector>

// Endian helpers
static inline uint16_t swap16(uint16_t val)
{
    return ((val & 0x00FF) << 8) | ((val & 0xFF00) >> 8);
}

static inline uint32_t swap32(uint32_t val)
{
    return ((val >> 24) & 0x000000FF) |
        ((val >>  8) & 0x0000FF00) |
        ((val <<  8) & 0x00FF0000) |
        ((val << 24) & 0xFF000000);
}

// BCD helpers for TOD clocks in CIA1 and CIA2
static inline uint8_t bcdToBinary(uint8_t bcd)
{
    return (bcd >> 4) * 10 + (bcd & 0x0F);
}

static inline uint8_t binaryToBCD(uint8_t binary)
{
    return ((binary / 10) << 4) | (binary % 10);
}

#endif // COMMON_H_INCLUDED
