// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "GCR/GCRTrackStream.h"

GCRTrackStream::GCRTrackStream() = default;

GCRTrackStream::~GCRTrackStream() = default;

void GCRTrackStream::clear()
{
    trackData.clear();
    syncMap.clear();
    speedZones.clear();
}

void GCRTrackStream::setTrackData(const std::vector<uint8_t>& data)
{
    trackData = data;
    rebuildSyncMap();
}

void GCRTrackStream::setSpeedZones(const std::vector<uint8_t>& zones)
{
    speedZones = zones;
}

const std::vector<uint8_t>& GCRTrackStream::getTrackData() const
{
    return trackData;
}

const std::vector<uint8_t>& GCRTrackStream::getSyncMap() const
{
    return syncMap;
}

const std::vector<uint8_t>& GCRTrackStream::getSpeedZones() const
{
    return speedZones;
}

bool GCRTrackStream::empty() const
{
    return trackData.empty();
}

size_t GCRTrackStream::size() const
{
    return trackData.size();
}

void GCRTrackStream::rebuildSyncMap()
{
    if (trackData.empty())
    {
        syncMap.clear();
        return;
    }

    const size_t n = trackData.size();

    syncMap.assign(n, 0);

    //
    // Scan the raw GCR track as a circular bitstream.
    // A 1541/1571 sync condition is reached after a run
    // of at least 10 consecutive 1 bits.
    //
    int oneRun = 0;

    //
    // Scan two revolutions so sync runs crossing the
    // end/start boundary are detected correctly.
    //
    for (size_t bitIndex = 0; bitIndex < n * 16; ++bitIndex)
    {
        const size_t wrappedBit = bitIndex % (n * 8);
        const size_t byteIndex = wrappedBit / 8;
        const int bitInByte = 7 - static_cast<int>(wrappedBit % 8);
        const bool bit = ((trackData[byteIndex] >> bitInByte) & 0x01) != 0;

        if (bit)
        {
            ++oneRun;

            if (oneRun >= 10 && bitIndex >= n * 8)
                syncMap[byteIndex] = 1;
        }
        else
        {
            oneRun = 0;
        }
    }
}
