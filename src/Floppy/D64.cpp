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

    if (!loadDiskImage(filePath))
        return false;

    const size_t sz = fileImageBuffer.size();

    int numTracks = 0;

    if (sz == D64_STANDARD_SIZE_35 || sz == D64_STANDARD_SIZE_35_ERR)      numTracks = 35;
    else if (sz == D64_STANDARD_SIZE_40 || sz == D64_STANDARD_SIZE_40_ERR) numTracks = 40;
    else if (sz == D64_STANDARD_SIZE_42 || sz == D64_STANDARD_SIZE_42_ERR) numTracks = 42;
    else
        return false;

    // Build geometry for the detected track count
    geom.sectorsPerTrack.resize(numTracks);
    for (int t = 1; t <= numTracks; ++t)
        geom.sectorsPerTrack[t - 1] = getSectorsForTrack(static_cast<uint8_t>(t));

    geom.trackOffsets.resize(numTracks);
    size_t offset = 0;
    for (int t = 1; t <= numTracks; ++t)
    {
        geom.trackOffsets[t - 1] = offset;
        offset += size_t(geom.sectorsPerTrack[t - 1]) * SECTOR_SIZE;
    }

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
    if (track >= 1  && track <= 17) return 21;
    if (track >= 18 && track <= 24) return 19;
    if (track >= 25 && track <= 30) return 18;
    if (track >= 31 && track <= 42) return 17;

    throw std::out_of_range("Invalid track number provided");
}

bool D64::validateDiskImage()
{
    const size_t sz = fileImageBuffer.size();

    // Fast-path: known common sizes
    if (sz == D64_STANDARD_SIZE_35 || sz == D64_STANDARD_SIZE_35_ERR ||
        sz == D64_STANDARD_SIZE_40 || sz == D64_STANDARD_SIZE_40_ERR ||
        sz == D64_STANDARD_SIZE_42 || sz == D64_STANDARD_SIZE_42_ERR)
    {
        return true;
    }

    return false;
}
