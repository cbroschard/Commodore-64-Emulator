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
    if (track < 1 || track > 80)
        throw std::out_of_range("Invalid track number provided");

    // D71 is two 35-track 1541-style sides.
    // Image tracks:
    //   1-35  = side 0 physical tracks 1-35
    //   36-70 = side 1 physical tracks 1-35
    //
    // Extended 80-track images keep 71-80 as extra 17-sector tracks.
    uint8_t physicalTrack = track;

    if (track >= 36 && track <= 70)
        physicalTrack = static_cast<uint8_t>(track - 35);

    if (physicalTrack <= 17) return 21;
    if (physicalTrack <= 24) return 19;
    if (physicalTrack <= 30) return 18;
    return 17;
}

bool D71::validateDiskImage()
{
    const size_t sz = fileImageBuffer.size();

    return (sz == D71_STANDARD_SIZE_70     || sz == D71_STANDARD_SIZE_70_ERR ||
            sz == D71_EXTENDED_SIZE_80     || sz == D71_EXTENDED_SIZE_80_ERR);
}

void D71::initializeGeometryForBlankImage()
{
    geom.hasPerSectorCRC = false;

    const uint8_t numTracks = 70; // standard D71

    geom.sectorsPerTrack.clear();
    geom.sectorsPerTrack.resize(numTracks);

    for (int t = 1; t <= numTracks; ++t)
        geom.sectorsPerTrack[t - 1] = getSectorsForTrack(static_cast<uint8_t>(t));

    geom.trackOffsets.clear();
    geom.trackOffsets.resize(numTracks);

    size_t offset = 0;
    for (int t = 1; t <= numTracks; ++t)
    {
        geom.trackOffsets[t - 1] = offset;
        offset += static_cast<size_t>(geom.sectorsPerTrack[t - 1]) * sectorSize();
    }
}

void D71::initializeBlankImageBuffer()
{
    if (geom.sectorsPerTrack.empty())
        initializeGeometryForBlankImage();

    size_t totalSectors = 0;

    for (int spt : geom.sectorsPerTrack)
        totalSectors += static_cast<size_t>(spt);

    fileImageBuffer.assign(totalSectors * sectorSize(), 0x00);
}

bool D71::writeBlankBAM(const std::string& volumeName, const std::string& volumeID)
{
    std::vector<uint8_t> bam0(sectorSize(), 0x00);
    std::vector<uint8_t> bam1(sectorSize(), 0x00);

    auto initBamHeader = [&](std::vector<uint8_t>& bam, bool primary)
    {
        if (primary)
        {
            // Track/Sector link to first directory sector.
            bam[0] = directoryStart.track;   // 18
            bam[1] = directoryStart.sector;  // 1
            bam[3] = 0x80; // Double sided flag
        }
        else
        {
            // No directory chain on side 1 BAM.
            bam[0] = 0;
            bam[1] = 0;
            bam[3] = 0;
        }

        bam[2] = 'A';

        for (size_t i = 0; i < 16; ++i)
            bam[0x90 + i] = i < volumeName.size() ? static_cast<uint8_t>(std::toupper(static_cast<unsigned char>(volumeName[i]))) : 0xA0;

        const char id0 = volumeID.size() > 0 ? volumeID[0] : '0';
        const char id1 = volumeID.size() > 1 ? volumeID[1] : '1';

        bam[0xA0] = 0xA0;
        bam[0xA1] = 0xA0;
        bam[0xA2] = static_cast<uint8_t>(std::toupper(static_cast<unsigned char>(id0)));
        bam[0xA3] = static_cast<uint8_t>(std::toupper(static_cast<unsigned char>(id1)));
        bam[0xA4] = 0xA0;

        bam[0xA5] = '2';
        bam[0xA6] = 'A';
        bam[0xA7] = 0xA0;
    };

    auto fillBamRange = [&](std::vector<uint8_t>& bam, uint8_t firstTrack, uint8_t lastTrack)
    {
        for (uint8_t track = firstTrack; track <= lastTrack; ++track)
        {
            const uint8_t localTrack = static_cast<uint8_t>(track - firstTrack + 1);
            const uint8_t spt = static_cast<uint8_t>(getSectorsForTrack(track));
            const size_t entry = 4 + static_cast<size_t>(localTrack - 1) * 4;

            bam[entry + 0] = spt;

            for (uint8_t sector = 0; sector < spt; ++sector)
            {
                bam[entry + 1 + (sector / 8)] |= static_cast<uint8_t>(1u << (sector % 8));
            }
        }
    };

    auto markUsed = [&](std::vector<uint8_t>& bam, uint8_t firstTrack, uint8_t track, uint8_t sector)
    {
        const uint8_t localTrack = static_cast<uint8_t>(track - firstTrack + 1);
        const size_t entry = 4 + static_cast<size_t>(localTrack - 1) * 4;
        const size_t byteIndex = entry + 1 + (sector / 8);
        const uint8_t bitMask = static_cast<uint8_t>(1u << (sector % 8));

        if (bam[byteIndex] & bitMask)
        {
            bam[byteIndex] &= static_cast<uint8_t>(~bitMask);
            --bam[entry];
        }
    };

    initBamHeader(bam0, true);
    initBamHeader(bam1, false);

    fillBamRange(bam0, 1, 35);
    fillBamRange(bam1, 36, 70);

    // Reserve the entire directory/BAM track on side 0.
    // This matches the normal CBM DOS "664 blocks free" behavior for one side.
    for (uint8_t sector = 0; sector < getSectorsForTrack(18); ++sector)
    {
        markUsed(bam0, 1, 18, sector);
    }

    // Reserve the entire BAM track on side 1.
    // This gives the second side another 664 free blocks.
    for (uint8_t sector = 0; sector < getSectorsForTrack(53); ++sector)
    {
        markUsed(bam1, 36, 53, sector);
    }

    if (!writeSector(18, 0, bam0))
        return false;

    if (!writeSector(53, 0, bam1))
        return false;

    return true;
}

bool D71::writeBlankDirectory()
{
    std::vector<uint8_t> dir(sectorSize(), 0x00);

    // End of directory chain.
    dir[0] = 0x00;
    dir[1] = 0xFF;

    return writeSector(directoryStart.track, directoryStart.sector, dir);
}
