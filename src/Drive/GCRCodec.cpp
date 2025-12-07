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

int GCRCodec::sectorsPerTrack1541(int track1based) const
{
    if (track1based <= 17) return 21;
    if (track1based <= 24) return 19;
    if (track1based <= 30) return 18;
    return 17; // 31..35
}
