// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Floppy/D81.h"

D81::D81()
{
    bamLocations = { {40,1}, {40,2} };
    directoryStart = {40,3};
}

D81::~D81() = default;

bool D81::loadDisk(const std::string& filePath)
{
    geom.hasPerSectorCRC = false;
    geom.sectorsPerTrack.assign(D81_TRACK_COUNT, D81_SECTORS_PER_TRACK);

    geom.trackOffsets.resize(D81_TRACK_COUNT);
    {
        size_t offset = D81_HEADER_SIZE;
        for (int i = 0; i < D81_TRACK_COUNT; ++i)
        {
            geom.trackOffsets[i] = offset;
            offset += size_t(D81_SECTORS_PER_TRACK) * sectorSize();
        }
    }
    if (!loadDiskImage(filePath)) return false;

    // No shrink step needed—.d81 is always 80 tracks.
    return true;
}

bool D81::saveDisk(const std::string& filePath)
{
    if (fileImageBuffer.empty())
    {
        std::cerr << "Error: No disk image loaded to save!\n";
        return false;
    }
    std::ofstream out(filePath, std::ios::binary);
    if (!out.is_open())
    {
        std::cerr << "Error: Unable to open " << filePath << " for writing\n";
        return false;
    }
    out.write(reinterpret_cast<const char*>(fileImageBuffer.data()),
              fileImageBuffer.size());
    return out.good();
}

const std::vector<uint8_t>& D81::getRawImage() const
{
    return fileImageBuffer;
}

uint16_t D81::getSectorsForTrack(uint8_t track)
{
    if (track < 1 || track > D81_TRACK_COUNT)
    {
        throw std::out_of_range("D81: track out of range");
    }
    return D81_SECTORS_PER_TRACK;
}

bool D81::validateDiskImage()
{
    size_t expected = D81_HEADER_SIZE + size_t(D81_TRACK_COUNT) * D81_SECTORS_PER_TRACK * sectorSize();
    if (fileImageBuffer.size() != expected)
    {
        std::cerr << "D81: unexpected image size (" << fileImageBuffer.size() << " vs " << expected << ")\n";
        return false;
    }
    return true;
}
