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

void D64::initializeGeometryForBlankImage()
{
    geom.sectorsPerTrack.clear();

    for (int t = 1; t <= 35; ++t)
    {
        if (t <= 17)
            geom.sectorsPerTrack.push_back(21);
        else if (t <= 24)
            geom.sectorsPerTrack.push_back(19);
        else if (t <= 30)
            geom.sectorsPerTrack.push_back(18);
        else
            geom.sectorsPerTrack.push_back(17);
    }

    geom.trackOffsets.clear();

    size_t offset = 0;
    for (int spt : geom.sectorsPerTrack)
    {
        geom.trackOffsets.push_back(offset);
        offset += static_cast<size_t>(spt) * sectorSize();
    }

    bamLocations.clear();
    bamLocations.push_back({18, 0});

    directoryStart = {18, 1};
}

void D64::initializeBlankImageBuffer()
{
    size_t totalSectors = 0;

    for (int spt : geom.sectorsPerTrack)
        totalSectors += static_cast<size_t>(spt);

    fileImageBuffer.assign(totalSectors * sectorSize(), 0x00);
}

bool D64::writeBlankBAM(const std::string& volumeName,
                        const std::string& volumeID)
{
    std::vector<uint8_t> bam(sectorSize(), 0x00);

    // Track/Sector link to first directory sector.
    bam[0] = directoryStart.track;   // 18
    bam[1] = directoryStart.sector;  // 1

    // DOS version type.
    bam[2] = 'A';

    // Build BAM entries for tracks 1-35.
    for (uint8_t track = 1; track <= 35; ++track)
    {
        const uint8_t spt = static_cast<uint8_t>(getSectorsForTrack(track));
        const size_t entry = 4 + static_cast<size_t>(track - 1) * 4;

        bam[entry + 0] = spt;

        for (uint8_t sector = 0; sector < spt; ++sector)
        {
            bam[entry + 1 + (sector / 8)] |= static_cast<uint8_t>(1u << (sector % 8));
        }
    }

    auto markUsed = [&](uint8_t track, uint8_t sector)
    {
        const size_t entry = 4 + static_cast<size_t>(track - 1) * 4;
        const size_t byteIndex = entry + 1 + (sector / 8);
        const uint8_t bitMask = static_cast<uint8_t>(1u << (sector % 8));

        if (bam[byteIndex] & bitMask)
        {
            bam[byteIndex] &= static_cast<uint8_t>(~bitMask);
            --bam[entry];
        }
    };

    // Reserve BAM/header and first directory sector.
    markUsed(18, 0);
    markUsed(18, 1);

    // Disk name, padded with shifted spaces.
    for (size_t i = 0; i < 16; ++i)
    {
        bam[0x90 + i] =
            i < volumeName.size()
                ? asciiToPetscii(static_cast<unsigned char>(std::toupper(volumeName[i])))
                : 0xA0;
    }

    const char id0 = volumeID.size() > 0 ? volumeID[0] : '0';
    const char id1 = volumeID.size() > 1 ? volumeID[1] : '1';

    bam[0xA0] = 0xA0;
    bam[0xA1] = 0xA0;
    bam[0xA2] = asciiToPetscii(static_cast<unsigned char>(std::toupper(id0)));
    bam[0xA3] = asciiToPetscii(static_cast<unsigned char>(std::toupper(id1)));
    bam[0xA4] = 0xA0;

    bam[0xA5] = '2';
    bam[0xA6] = 'A';
    bam[0xA7] = 0xA0;

    return writeSector(18, 0, bam);
}

bool D64::writeBlankDirectory()
{
    std::vector<uint8_t> dir(sectorSize(), 0x00);

    // End of directory chain.
    dir[0] = 0x00;
    dir[1] = 0xFF;

    return writeSector(directoryStart.track, directoryStart.sector, dir);
}
