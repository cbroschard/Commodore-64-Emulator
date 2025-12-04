// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Floppy/D71.h"

D71::D71()
{
    bamLocations = {{18,0}, {53,0}};
    directoryStart = {18,1};
}

D71::~D71() = default;

bool D71::loadDisk(const std::string& filePath)
{
    geom.hasPerSectorCRC = false;

    if (!loadDiskImage(filePath)) return false;

    const size_t sz = fileImageBuffer.size();

    uint8_t numTracks = 0;
    if (sz == D71_STANDARD_SIZE_70 || sz == D71_STANDARD_SIZE_70_ERR)      numTracks = 70;
    else if (sz == D71_EXTENDED_SIZE_80 || sz == D71_EXTENDED_SIZE_80_ERR) numTracks = 80;
    else return false;

    geom.sectorsPerTrack.resize(numTracks);
    for (int t = 1; t <= numTracks; ++t)
        geom.sectorsPerTrack[t-1] = getSectorsForTrack(uint8_t(t));

    geom.trackOffsets.resize(numTracks);
    size_t offset = 0;
    for (int t = 1; t <= numTracks; ++t)
    {
        geom.trackOffsets[t-1] = offset;
        offset += size_t(geom.sectorsPerTrack[t-1]) * SECTOR_SIZE;
    }

    return true;
}

bool D71::saveDisk(const std::string& filePath)
{
    if (fileImageBuffer.empty())
    {
        std::cerr << "Error: No disk image loaded to save!\n";
        return false;
    }
    std::ofstream out(filePath, std::ios::binary);
    if (!out.is_open())
    {
        std::cerr << "Error opening " << filePath << " for writing\n";
        return false;
    }
    out.write(reinterpret_cast<const char*>(fileImageBuffer.data()), fileImageBuffer.size());
    return out.good();
}

const std::vector<uint8_t>& D71::getRawImage() const
{
    return fileImageBuffer;
}

uint16_t D71::getSectorsForTrack(uint8_t track)
{
    static const std::vector<TrackSectorInfo> trackSectorInfo =
    {
        {1,  17, 21},
        {18, 24, 19},
        {25, 30, 18},
        {31, 40, 17},
        {41, 57, 21},
        {58, 64, 19},
        {65, 70, 18},
        {71, 80, 17}
    };

    for (const auto& info : trackSectorInfo)
    {
        if (track >= info.startTrack && track <= info.endTrack)
        {
            return info.numSectors;
        }
    }

    // Throw an exception if not found
     throw std::out_of_range("Invalid track number provided");
}

bool D71::validateDiskImage()
{
    const size_t sz = fileImageBuffer.size();

    return (sz == D71_STANDARD_SIZE_70     || sz == D71_STANDARD_SIZE_70_ERR ||
            sz == D71_EXTENDED_SIZE_80     || sz == D71_EXTENDED_SIZE_80_ERR);
}
