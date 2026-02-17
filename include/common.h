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

// Cartridge memory location
enum cartLocation { LO, HI };

// Program loading Constants
static const uint16_t BASIC_PRG_START = 0x0801;
static const uint16_t TXTAB = 0x002B;
static const uint16_t VARTAB = 0x002D;
static const uint16_t ARYTAB = 0x002F;
static const uint16_t STREND = 0x0031;

// Logging
enum class LogSet { Cartridge, Cassette, CIA1, CIA2, CPU, IO, Joystick, Keyboard, Memory, PLA, VIC };

// Convert ASCII text to PETSCII
inline uint8_t asciiToPetscii(char c)
{
    // Uppercase A–Z map directly (ASCII == PETSCII in uppercase mode)
    if (c >= 'A' && c <= 'Z') return c;

    // Digits 0–9 map directly
    if (c >= '0' && c <= '9') return c;

    // Space and common punctuation map directly too
    switch (c)
    {
        case ' ': return 0x20;
        case '.': return 0x2E;
        case '"': return 0x22;
        case '*': return 0x2A;
        case ',': return 0x2C;
        case ':': return 0x3A;
        case ';': return 0x3B;
    }

    // Lowercase a–z → PETSCII is shifted up by $80
    if (c >= 'a' && c <= 'z') return (c - 0x20) | 0x80;

    // Fallback to space
    return 0x20;
}

static inline std::string petsciiToAscii(const uint8_t* s, int n)
{
    std::string out;
    for (int i = 0; i < n; i++)
    {
        uint8_t c = s[i];
        if (c == 0xA0) break; // PETSCII space padding in filenames
        if (c >= 0x20 && c <= 0x7E) out.push_back(char(c));
        else out.push_back('.');
    }
    return out;
}

static inline void dumpDirBlock(const std::vector<uint8_t>& sec)
{
    std::cout << "[DIR] link=" << int(sec[0]) << "/" << int(sec[1]) << "\n";

    for (int i = 0; i < 8; i++)
    {
        int off = 0x02 + i * 0x20;
        uint8_t type = sec[off + 0];
        uint8_t t    = sec[off + 1];
        uint8_t s    = sec[off + 2];

        if (type == 0x00) continue; // unused -> end-ish

        std::string name = petsciiToAscii(&sec[off + 3], 16);
        uint16_t blocks  = uint16_t(sec[off + 0x1E]) | (uint16_t(sec[off + 0x1F]) << 8);

        std::cout << "[DIR] #" << i
                  << " type=$" << std::hex << int(type) << std::dec
                  << " start=" << int(t) << "/" << int(s)
                  << " blocks=" << blocks
                  << " name=\"" << name << "\"\n";
    }
}

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
