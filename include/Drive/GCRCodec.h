// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef GCRCODEC_H
#define GCRCODEC_H

#include <cstdint>
#include <vector>
#include <cstring>
#include <algorithm>

class GCRCodec
{
    public:
        GCRCodec();
        virtual ~GCRCodec();

        void encode4Bytes(const uint8_t in[4], uint8_t out[5]);
        void encodeBytes(const uint8_t* in, size_t len, std::vector<uint8_t>& out);

        int sectorsPerTrack1541(int track1based) const;

    protected:

    private:

        static constexpr uint8_t GCR5[16] =
        {
            0x0A, 0x0B, 0x12, 0x13,
            0x0E, 0x0F, 0x16, 0x17,
            0x09, 0x19, 0x1A, 0x1B,
            0x0D, 0x1D, 0x1E, 0x15
        };
};

#endif // GCRCODEC_H
