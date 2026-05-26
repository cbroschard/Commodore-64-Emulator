// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Floppy/D81.h"

namespace
{
    bool d81BamLocationForTrack(uint8_t track,
                                uint8_t& bamTrack,
                                uint8_t& bamSector,
                                size_t& entryOffset)
    {
        if (track < 1 || track > 80)
            return false;

        bamTrack = 40;

        if (track <= 40)
        {
            bamSector = 1;
            entryOffset = 16 + static_cast<size_t>(track - 1) * 6;
        }
        else
        {
            bamSector = 2;
            entryOffset = 16 + static_cast<size_t>(track - 41) * 6;
        }

        return true;
    }
}

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

bool D81::allocateSector(uint8_t& outTrack, uint8_t& outSector)
{
    auto tryTrack = [&](uint8_t track) -> bool
    {
        uint8_t bamTrack = 0;
        uint8_t bamSector = 0;
        size_t entry = 0;

        if (!d81BamLocationForTrack(track, bamTrack, bamSector, entry))
            return false;

        auto bam = readSector(bamTrack, bamSector);
        if (bam.size() != sectorSize())
            return false;

        if (bam[entry] == 0)
            return false;

        for (uint8_t sector = 0; sector < D81_SECTORS_PER_TRACK; ++sector)
        {
            const size_t byteIndex = entry + 1 + (sector / 8);
            const uint8_t bitMask = static_cast<uint8_t>(1u << (sector % 8));

            if (bam[byteIndex] & bitMask)
            {
                bam[byteIndex] &= static_cast<uint8_t>(~bitMask);
                --bam[entry];

                if (!writeSector(bamTrack, bamSector, bam))
                    return false;

                outTrack = track;
                outSector = sector;
                return true;
            }
        }

        return false;
    };

    // Prefer data tracks outside the system track.
    for (uint8_t track = 41; track <= 80; ++track)
    {
        if (tryTrack(track))
            return true;
    }

    for (uint8_t track = 1; track <= 39; ++track)
    {
        if (tryTrack(track))
            return true;
    }

    // Last resort: use remaining non-system sectors on track 40.
    uint8_t bamTrack = 0;
    uint8_t bamSector = 0;
    size_t entry = 0;

    if (!d81BamLocationForTrack(40, bamTrack, bamSector, entry))
        return false;

    auto bam = readSector(bamTrack, bamSector);
    if (bam.size() != sectorSize())
        return false;

    for (uint8_t sector = 4; sector < D81_SECTORS_PER_TRACK; ++sector)
    {
        const size_t byteIndex = entry + 1 + (sector / 8);
        const uint8_t bitMask = static_cast<uint8_t>(1u << (sector % 8));

        if (bam[byteIndex] & bitMask)
        {
            bam[byteIndex] &= static_cast<uint8_t>(~bitMask);
            --bam[entry];

            if (!writeSector(bamTrack, bamSector, bam))
                return false;

            outTrack = 40;
            outSector = sector;
            return true;
        }
    }

    return false;
}

void D81::freeSector(uint8_t track, uint8_t sector)
{
    if (track < 1 || track > D81_TRACK_COUNT || sector >= D81_SECTORS_PER_TRACK)
        return;

    // Never free header/BAM/directory root sectors.
    if (track == 40 && sector <= 3)
        return;

    uint8_t bamTrack = 0;
    uint8_t bamSector = 0;
    size_t entry = 0;

    if (!d81BamLocationForTrack(track, bamTrack, bamSector, entry))
        return;

    auto bam = readSector(bamTrack, bamSector);
    if (bam.size() != sectorSize())
        return;

    const size_t byteIndex = entry + 1 + (sector / 8);
    const uint8_t bitMask = static_cast<uint8_t>(1u << (sector % 8));

    if ((bam[byteIndex] & bitMask) == 0)
    {
        bam[byteIndex] |= bitMask;
        ++bam[entry];

        writeSector(bamTrack, bamSector, bam);
    }
}

void D81::initializeGeometryForBlankImage()
{
    geom.hasPerSectorCRC = false;

    geom.sectorsPerTrack.clear();
    geom.sectorsPerTrack.resize(D81_TRACK_COUNT, D81_SECTORS_PER_TRACK);

    geom.trackOffsets.clear();
    geom.trackOffsets.resize(D81_TRACK_COUNT);

    size_t offset = D81_HEADER_SIZE;

    for (int t = 1; t <= D81_TRACK_COUNT; ++t)
    {
        geom.trackOffsets[t - 1] = offset;
        offset += static_cast<size_t>(D81_SECTORS_PER_TRACK) * sectorSize();
    }

    bamLocations.clear();
    bamLocations.push_back({40, 1});
    bamLocations.push_back({40, 2});

    directoryStart = {40, 3};
}

void D81::initializeBlankImageBuffer()
{
    if (geom.sectorsPerTrack.empty() || geom.trackOffsets.empty())
        initializeGeometryForBlankImage();

    const size_t totalSize =
        D81_HEADER_SIZE +
        static_cast<size_t>(D81_TRACK_COUNT) *
        static_cast<size_t>(D81_SECTORS_PER_TRACK) *
        sectorSize();

    fileImageBuffer.assign(totalSize, 0x00);
}

bool D81::writeBlankBAM(const std::string& volumeName, const std::string& volumeID)
{
    std::vector<uint8_t> header(sectorSize(), 0x00);
    std::vector<uint8_t> bam1(sectorSize(), 0x00);
    std::vector<uint8_t> bam2(sectorSize(), 0x00);

    auto petName = [&](size_t index) -> uint8_t
    {
        if (index < volumeName.size())
            return static_cast<uint8_t>(std::toupper(static_cast<unsigned char>(volumeName[index])));
        return 0xA0;
    };

    const char id0 = volumeID.size() > 0 ? volumeID[0] : '0';
    const char id1 = volumeID.size() > 1 ? volumeID[1] : '1';

    // Track 40 sector 0: D81 header.
    header[0] = 40;
    header[1] = 3;
    header[2] = 'D';

    for (size_t i = 0; i < 16; ++i)
        header[4 + i] = petName(i);

    header[22] = static_cast<uint8_t>(std::toupper(static_cast<unsigned char>(id0)));
    header[23] = static_cast<uint8_t>(std::toupper(static_cast<unsigned char>(id1)));
    header[24] = 0xA0;
    header[25] = '3';
    header[26] = 'D';
    header[27] = 0xA0;

    const uint8_t diskId0 = static_cast<uint8_t>(std::toupper(static_cast<unsigned char>(id0)));
    const uint8_t diskId1 = static_cast<uint8_t>(std::toupper(static_cast<unsigned char>(id1)));

    // BAM sector 1: tracks 1-40.
    // Link to second BAM sector at 40/2.
    bam1[0] = 40;
    bam1[1] = 2;
    bam1[2] = 'D';
    bam1[3] = 0xBB; // complement of 'D'
    bam1[4] = diskId0;
    bam1[5] = diskId1;
    bam1[6] = 0xC0; // I/O byte: verify off / header CRC behavior
    bam1[7] = 0x00; // autoboot flag off

    // Keep reserved header area deterministic.
    for (int i = 8; i < 16; ++i)
        bam1[i] = 0x00;

    // BAM sector 2: tracks 41-80.
    // End of BAM chain.
    bam2[0] = 0x00;
    bam2[1] = 0xFF;
    bam2[2] = 'D';
    bam2[3] = 0xBB;
    bam2[4] = diskId0;
    bam2[5] = diskId1;
    bam2[6] = 0xC0;
    bam2[7] = 0x00;

    for (int i = 8; i < 16; ++i)
        bam2[i] = 0x00;

    auto fillBamRange = [&](std::vector<uint8_t>& bam, uint8_t firstTrack, uint8_t lastTrack)
    {
        for (uint8_t track = firstTrack; track <= lastTrack; ++track)
        {
            const uint8_t localTrack = static_cast<uint8_t>(track - firstTrack + 1);
            const size_t entry = 16 + static_cast<size_t>(localTrack - 1) * 6;

            bam[entry + 0] = D81_SECTORS_PER_TRACK;

            // 40 sectors = 5 bitmap bytes.
            bam[entry + 1] = 0xFF;
            bam[entry + 2] = 0xFF;
            bam[entry + 3] = 0xFF;
            bam[entry + 4] = 0xFF;
            bam[entry + 5] = 0xFF;
        }
    };

    auto markUsed = [&](std::vector<uint8_t>& bam, uint8_t firstTrack, uint8_t track, uint8_t sector)
    {
        const uint8_t localTrack = static_cast<uint8_t>(track - firstTrack + 1);
        const size_t entry = 16 + static_cast<size_t>(localTrack - 1) * 6;
        const size_t byteIndex = entry + 1 + (sector / 8);
        const uint8_t bitMask = static_cast<uint8_t>(1u << (sector % 8));

        if (bam[byteIndex] & bitMask)
        {
            bam[byteIndex] &= static_cast<uint8_t>(~bitMask);
            --bam[entry];
        }
    };

    fillBamRange(bam1, 1, 40);
    fillBamRange(bam2, 41, 80);

    // Reserve header, BAM1, BAM2, and first directory sector on track 40.
    markUsed(bam1, 1, 40, 0);
    markUsed(bam1, 1, 40, 1);
    markUsed(bam1, 1, 40, 2);
    markUsed(bam1, 1, 40, 3);

    if (!writeSector(40, 0, header))
        return false;

    if (!writeSector(40, 1, bam1))
        return false;

    if (!writeSector(40, 2, bam2))
        return false;

    return true;
}

bool D81::writeBlankDirectory()
{
    std::vector<uint8_t> dir(sectorSize(), 0x00);

    // End of directory chain.
    dir[0] = 0x00;
    dir[1] = 0xFF;

    return writeSector(directoryStart.track, directoryStart.sector, dir);
}
