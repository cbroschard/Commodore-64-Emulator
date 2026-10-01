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

    if (sz == D71_STANDARD_SIZE_70 || sz == D71_STANDARD_SIZE_70_ERR)
            numTracks = 70;
    else if (sz == D71_EXTENDED_SIZE_80 || sz == D71_EXTENDED_SIZE_80_ERR)
        numTracks = 80;
    else
        return false;

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

    if (physicalTrack <= 17)
        return 21;

    if (physicalTrack <= 24)
        return 19;

    if (physicalTrack <= 30)
        return 18;

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

bool D71::writeBlankBAM(const std::string& volumeName,
                        const std::string& volumeID)
{
    // D71 BAM layout:
    //
    // Track 18 / Sector 0:
    //   $00-$03 : BAM/header
    //   $04-$8F : tracks 1-35
    //             4 bytes per track:
    //               free-sector count
    //               3-byte bitmap
    //
    //   $DD-$FF : free-sector counts for tracks 36-70
    //
    // Track 53 / Sector 0:
    //   $00-$68 : allocation bitmaps for tracks 36-70
    //             3 bytes per track

    std::vector<uint8_t> bam0(sectorSize(), 0x00);
    std::vector<uint8_t> bam1(sectorSize(), 0x00);

    // ---------------------------------------------------------
    // Primary BAM header - Track 18 / Sector 0
    // ---------------------------------------------------------

    bam0[0x00] = directoryStart.track;   // 18
    bam0[0x01] = directoryStart.sector;  // 1
    bam0[0x02] = 'A';
    bam0[0x03] = 0x80;                   // Double-sided flag

    // Disk name.
    for (size_t i = 0; i < 16; ++i)
    {
        bam0[0x90 + i] =
            (i < volumeName.size())
                ? static_cast<uint8_t>(
                      std::toupper(
                          static_cast<unsigned char>(volumeName[i])))
                : 0xA0;
    }

    const uint8_t id0 =
        volumeID.size() > 0
            ? static_cast<uint8_t>(
                  std::toupper(
                      static_cast<unsigned char>(volumeID[0])))
            : static_cast<uint8_t>('0');

    const uint8_t id1 =
        volumeID.size() > 1
            ? static_cast<uint8_t>(
                  std::toupper(
                      static_cast<unsigned char>(volumeID[1])))
            : static_cast<uint8_t>('1');

    bam0[0xA0] = 0xA0;
    bam0[0xA1] = 0xA0;

    bam0[0xA2] = id0;
    bam0[0xA3] = id1;

    bam0[0xA4] = 0xA0;
    bam0[0xA5] = '2';
    bam0[0xA6] = 'A';
    bam0[0xA7] = 0xA0;

    // ---------------------------------------------------------
    // Tracks 1-35:
    // 4 bytes per track in primary BAM.
    // ---------------------------------------------------------

    for (uint8_t track = 1; track <= 35; ++track)
    {
        const uint8_t spt =
            static_cast<uint8_t>(getSectorsForTrack(track));

        const size_t entry =
            0x04 + static_cast<size_t>(track - 1) * 4;

        bam0[entry + 0] = spt;

        for (uint8_t sector = 0; sector < spt; ++sector)
        {
            const size_t bitmapByte =
                entry + 1 + static_cast<size_t>(sector / 8);

            bam0[bitmapByte] |=
                static_cast<uint8_t>(1u << (sector % 8));
        }
    }

    // ---------------------------------------------------------
    // Reserve all of track 18.
    //
    // Since this track contains the BAM/directory, DOS should
    // report no free sectors on it.
    // ---------------------------------------------------------

    {
        constexpr uint8_t track = 18;

        const size_t entry =
            0x04 + static_cast<size_t>(track - 1) * 4;

        bam0[entry + 0] = 0x00;
        bam0[entry + 1] = 0x00;
        bam0[entry + 2] = 0x00;
        bam0[entry + 3] = 0x00;
    }

    // ---------------------------------------------------------
    // Tracks 36-70.
    //
    // Free counts live in primary BAM:
    //
    //   Track 36 -> $DD
    //   Track 37 -> $DE
    //   ...
    //   Track 70 -> $FF
    //
    // Allocation bitmaps live in track 53/sector 0:
    //
    //   Track 36 -> $00-$02
    //   Track 37 -> $03-$05
    //   ...
    //   Track 70 -> $66-$68
    // ---------------------------------------------------------

    for (uint8_t track = 36; track <= 70; ++track)
    {
        const uint8_t spt =
            static_cast<uint8_t>(getSectorsForTrack(track));

        const size_t sideTrack =
            static_cast<size_t>(track - 36);

        const size_t countOffset =
            0xDD + sideTrack;

        const size_t bitmapOffset =
            sideTrack * 3;

        bam0[countOffset] = spt;

        for (uint8_t sector = 0; sector < spt; ++sector)
        {
            bam1[bitmapOffset + (sector / 8)] |=
                static_cast<uint8_t>(1u << (sector % 8));
        }
    }

    // ---------------------------------------------------------
    // Reserve track 53.
    //
    // Track 53 is physical track 18 on side 2 and contains
    // the secondary BAM bitmap sector.
    // ---------------------------------------------------------

    {
        constexpr uint8_t track = 53;

        const size_t sideTrack =
            static_cast<size_t>(track - 36);

        const size_t countOffset =
            0xDD + sideTrack;

        const size_t bitmapOffset =
            sideTrack * 3;

        bam0[countOffset] = 0x00;

        bam1[bitmapOffset + 0] = 0x00;
        bam1[bitmapOffset + 1] = 0x00;
        bam1[bitmapOffset + 2] = 0x00;
    }

    // ---------------------------------------------------------
    // Write both BAM sectors.
    // ---------------------------------------------------------

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
