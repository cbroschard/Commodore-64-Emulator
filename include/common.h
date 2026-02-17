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
