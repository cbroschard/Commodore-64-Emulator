// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "GCRCodec.h"

GCRCodec::GCRCodec() = default;

GCRCodec::~GCRCodec() = default;

void GCRCodec::encode4Bytes(const uint8_t in[4], uint8_t out[5])
{
    uint8_t n[8] = {
        uint8_t(in[0] >> 4), uint8_t(in[0] & 0x0F),
        uint8_t(in[1] >> 4), uint8_t(in[1] & 0x0F),
        uint8_t(in[2] >> 4), uint8_t(in[2] & 0x0F),
        uint8_t(in[3] >> 4), uint8_t(in[3] & 0x0F),
    };

    uint64_t bits = 0;
    for (int i = 0; i < 8; i++)
        bits = (bits << 5) | (GCR5[n[i]] & 0x1F);

    out[0] = uint8_t((bits >> 32) & 0xFF);
    out[1] = uint8_t((bits >> 24) & 0xFF);
    out[2] = uint8_t((bits >> 16) & 0xFF);
    out[3] = uint8_t((bits >>  8) & 0xFF);
    out[4] = uint8_t((bits >>  0) & 0xFF);
}

void GCRCodec::encodeBytes(const uint8_t* in, size_t len, std::vector<uint8_t>& out)
{
    for (size_t i = 0; i < len; i += 4)
    {
        uint8_t g[5];
        encode4Bytes(&in[i], g);
        out.insert(out.end(), g, g + 5);
    }
}

static int decodeGCRNibble(uint8_t code)
{
    switch (code & 0x1F)
    {
        case 0x0A: return 0x0;
        case 0x0B: return 0x1;
        case 0x12: return 0x2;
        case 0x13: return 0x3;
        case 0x0E: return 0x4;
        case 0x0F: return 0x5;
        case 0x16: return 0x6;
        case 0x17: return 0x7;
        case 0x09: return 0x8;
        case 0x19: return 0x9;
        case 0x1A: return 0xA;
        case 0x1B: return 0xB;
        case 0x0D: return 0xC;
        case 0x1D: return 0xD;
        case 0x1E: return 0xE;
        case 0x15: return 0xF;
        default:   return -1;
    }
}

bool GCRCodec::decode5Bytes(const uint8_t in[5], uint8_t out[4]) const
{
    uint64_t bits = 0;

    bits |= static_cast<uint64_t>(in[0]) << 32;
    bits |= static_cast<uint64_t>(in[1]) << 24;
    bits |= static_cast<uint64_t>(in[2]) << 16;
    bits |= static_cast<uint64_t>(in[3]) << 8;
    bits |= static_cast<uint64_t>(in[4]);

    uint8_t n[8] = {};

    for (int i = 0; i < 8; ++i)
    {
        const int shift = 35 - i * 5;
        const uint8_t code = static_cast<uint8_t>((bits >> shift) & 0x1F);

        const int nibble = decodeGCRNibble(code);
        if (nibble < 0)
            return false;

        n[i] = static_cast<uint8_t>(nibble);
    }

    out[0] = static_cast<uint8_t>((n[0] << 4) | n[1]);
    out[1] = static_cast<uint8_t>((n[2] << 4) | n[3]);
    out[2] = static_cast<uint8_t>((n[4] << 4) | n[5]);
    out[3] = static_cast<uint8_t>((n[6] << 4) | n[7]);

    return true;
}

bool GCRCodec::decodeBytes(const uint8_t* in, size_t len, std::vector<uint8_t>& out) const
{
    if ((len % 5) != 0)
        return false;

    for (size_t i = 0; i < len; i += 5)
    {
        uint8_t raw[4] = {};
        if (!decode5Bytes(&in[i], raw))
            return false;

        out.insert(out.end(), raw, raw + 4);
    }

    return true;
}

int GCRCodec::sectorsPerTrack1541(int track1based) const
{
    if (track1based <= 17) return 21;
    if (track1based <= 24) return 19;
    if (track1based <= 30) return 18;
    return 17; // 31..35
}
