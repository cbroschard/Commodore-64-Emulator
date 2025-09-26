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
#include <sdl2/sdl.h>
#include <sstream>
#include <thread>

// Cartridge memory location
enum cartLocation { LO, HI };

// Video mode
enum class VideoMode { NTSC, PAL};

// Program loading Constants
static const uint16_t BASIC_PRG_START = 0x0801;
static const uint16_t TXTAB = 0x002B;
static const uint16_t VARTAB = 0x002D;
static const uint16_t ARYTAB = 0x002F;
static const uint16_t STREND = 0x0031;

// Convert ASCII text to PETSCII
inline uint8_t asciiToScreenCode(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 0x40;  // 'A'(0x41)->1
    if (c >= '0' && c <= '9') return c;         // digits map directly
    if (c == ' ') return 0x20;                  // space
    if (c == '.') return 0x2E;                  // period
    if (c == '"') return 0x22;                  // quote
    if (c == ' ') return 0x20;                  // space
    if (c == '.') return 0x2E;                  // period
    if (c == '"') return 0x22;                  // quote
    if (c == '*') return 0x2A;                  // asterisk
    if (c == ',') return 0x2C;                  // comma
    if (c == ':') return 0x3A;                  // colon (in case)
    if (c == ';') return 0x3B;                  // semicolon (in case)
    return 0x20;                                // fallback: space
}

// Struct to hold the joystick 1 and 2 mappings from configuration file
struct JoystickMapping
{
    SDL_Scancode up;
    SDL_Scancode down;
    SDL_Scancode left;
    SDL_Scancode right;
    SDL_Scancode fire;
};

// Cassette helper struct
struct T64LoadResult
{
    bool success = false;
    uint16_t prgStart = 0;
    uint16_t prgEnd = 0;
};

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
