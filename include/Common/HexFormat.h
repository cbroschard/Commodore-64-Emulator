// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef HEXFORMAT_H_INCLUDED
#define HEXFORMAT_H_INCLUDED

#include <string>

// Hex helpers
inline std::string toHex(uint16_t value, int width = 4)
{
    std::stringstream ss;
    ss << std::uppercase << std::hex << std::setfill('0') << std::setw(width) << static_cast<int>(value);
    return ss.str();
}

inline std::string toHex(uint8_t value, int width = 2)
{
    std::stringstream ss;
    ss << std::uppercase << std::hex << std::setfill('0') << std::setw(width) << static_cast<int>(value);
    return ss.str();
}

#endif // HEXFORMAT_H_INCLUDED
