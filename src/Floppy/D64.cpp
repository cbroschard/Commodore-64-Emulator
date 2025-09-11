// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Floppy/D64.h"

D64::D64()
{
    bamLocations = {{18,0}};
    directoryStart = {18,1};
};

D64::~D64() = default;

bool D64::loadDisk(const std::string& filePath)
{
    geom.hasPerSectorCRC = false;
    geom.sectorsPerTrack.resize(40);
    for (int t = 1; t <= 40; ++t)
        geom.sectorsPerTrack[t-1] = getSectorsForTrack(t);

    geom.trackOffsets.resize(40);
    {
      size_t offset = 0;
      for (int t = 1; t <= 40; ++t) {
        geom.trackOffsets[t-1] = offset;
        offset += geom.sectorsPerTrack[t-1] * SECTOR_SIZE
             + (geom.hasPerSectorCRC ? geom.sectorsPerTrack[t-1]*2 : 0);
      }
    }

    if (!loadDiskImage(filePath)) return false;

    // Shrink geometry to the *actual* number of tracks (35 or 40)
    uint8_t numTracks = (fileImageBuffer.size() == D64_STANDARD_SIZE_35 ? 35 : 40);
    geom.sectorsPerTrack.resize(numTracks);
    geom.trackOffsets   .resize(numTracks);
    return true;
}

bool D64::saveDisk(const std::string& filePath)
{
    if (fileImageBuffer.empty())
    {
        std::cerr << "Error: No disk image loaded to save!" << std::endl;
        return false;
    }
    // Open the output file in binary mode.
    std::ofstream file(filePath, std::ios::binary);
    if (!file.is_open())
    {
        std::cerr << "Failed to open file for writing: " << filePath << std::endl;
        return false;
    }

    // Write the entire buffer to the file.
    file.write(reinterpret_cast<const char*>(fileImageBuffer.data()), fileImageBuffer.size());
    if (!file.good())
    {
        std::cerr << "Error occurred while writing to file: " << filePath << std::endl;
        return false;
    }
    std::cout << "Disk saved successfully to: " << filePath << std::endl;
    return true;
}

const std::vector<uint8_t>& D64::getRawImage() const
{
    return fileImageBuffer;
}

uint16_t D64::getSectorsForTrack(uint8_t track)
{
    static const std::vector<TrackSectorInfo> trackSectorInfo =
    {
        {1, 17, 21},
        {18, 24, 19},
        {25, 30, 18},
        {31, 40, 17}
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

bool D64::validateDiskImage()
{
    if (fileImageBuffer.size() != D64_STANDARD_SIZE_35 && fileImageBuffer.size() != D64_STANDARD_SIZE_40)
    {
        return false;
    }
    if (!validateHeader())
    {
        return false;
    }

    if (!validateDiskNameAndID())
    {
        return false;
    }

    if (!validateDirectoryChain())
    {
        return false;
    }

    // ALl checks passed
    return true;
}
