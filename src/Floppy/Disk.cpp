// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Floppy/Disk.h"

Disk::Disk() = default;

Disk::~Disk() = default;

bool Disk::loadDiskImage(const std::string& imagePath)
{
    std::ifstream file(imagePath, std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
        std::cerr << "Failed to open file: " << imagePath << std::endl;
        return false;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    fileImageBuffer.resize(size);
    if (!file.read(reinterpret_cast<char*>(fileImageBuffer.data()), size))
    {
        std::cerr << "Failed to read file: " << imagePath << std::endl;
        return false;
    }

    if (!validateDiskImage())
    {
        std::cerr << "Failed to validate the disk image, not a valid D64 image!" << imagePath << std::endl;
        return false;
    }

    #ifdef Debug
    std::cout << "Loaded file: " << imagePath << " (" << size << " bytes)" << std::endl;
    #endif // Debug
    return true;
}

size_t Disk::computeOffset(uint8_t track, uint8_t sector)
{
    auto sectorsInThisTrack = geom.sectorsPerTrack.at(track -1);
    if (sector >= sectorsInThisTrack)
        throw std::out_of_range("Sector number out of range");

    const size_t sz = sectorSize();

    size_t offset = geom.trackOffsets[track -1]
        + static_cast<size_t>(sector) * sz
        + (geom.hasPerSectorCRC ? static_cast<size_t>(sector) * 2 : 0);

    return offset;
}

std::vector<uint8_t> Disk::readSector(uint8_t track, uint8_t sector)
{
    const size_t sz = sectorSize();
    auto offset = computeOffset(track, sector);
    auto itBegin = fileImageBuffer.begin() + offset;
    auto itEnd   = itBegin + sz;
    return std::vector<uint8_t>(itBegin, itEnd);
}

bool Disk::writeSector(uint8_t track, uint16_t sector, const std::vector<uint8_t>& buf)
{
    const size_t sz = sectorSize();
    auto offset = computeOffset(track, sector);

    // Safety: don’t assume buf is the right size
    const size_t n = std::min(sz, buf.size());
    std::copy(buf.begin(), buf.begin() + n, fileImageBuffer.begin() + offset);

    // Optional: if buf smaller than sector, zero-fill remainder
    if (n < sz)
        std::fill(fileImageBuffer.begin() + offset + n, fileImageBuffer.begin() + offset + sz, 0x00);

    return true;
}
