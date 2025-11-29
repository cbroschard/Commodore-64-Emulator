// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "CBMImage.h"

CBMImage::CBMImage() = default;

CBMImage::~CBMImage() = default;

std::vector<uint8_t> CBMImage::getDirectoryListing()
{
    std::vector<uint8_t> listing;
    uint8_t track = directoryStart.track;
    uint8_t sector = directoryStart.sector;

    // Walk the directory chain
    while (track != 0)
    {
        auto sectorData = readSector(track, sector);
        uint8_t nextTrack  = sectorData[0];
        uint8_t nextSector = sectorData[1];

        // There are 8 entries per sector
        for (int entry = 0; entry < 8; ++entry)
        {
            size_t base = 2 + entry * 32;
            uint8_t ft = sectorData[base + 0];
            if (ft == 0x00) continue;  // unused slot

            static const char* types[] = { "DEL", "SEQ", "PRG", "USR", "REL" };
            uint8_t typeCode = ft & 0x07;
            const char* typeStr = (typeCode <= 4 ? types[typeCode] : "???");

            // Get start T/S
            uint8_t fileTrack  = sectorData[base + 1];
            uint8_t fileSector = sectorData[base + 2];

            // Read PETSCII filename bytes 3–18
            std::string name;
            for (int i = 0; i < 16; ++i)
            {
                uint8_t c = sectorData[base + 3 + i];
                name += (c == 0xA0 ? ' ' : (char)c);
            }
            // Safe trim
            auto pos = name.find_last_not_of(' ');
            if (pos == std::string::npos)
            {
                name.clear();
            }
            else
            {
                name.erase(pos + 1);
            }

            // Count blocks by following the T/S chain
            size_t blocks = 0;
            uint8_t t = fileTrack, s = fileSector;
            while (t != 0)
            {
                ++blocks;
                auto blk = readSector(t, s);
                t = blk[0];
                s = blk[1];
            }

            // Format a line like: " 10 \"FILENAME\" PRG\r"
            char line[40];
            snprintf(line, sizeof(line),
                     "%3zu \"%s\" %s\r",
                     blocks, name.c_str(), typeStr);
            for (char ch : std::string(line))
            {
                listing.push_back((uint8_t)ch);
            }
        }

        track  = nextTrack;
        sector = nextSector;
    }

    // Compute total free blocks from BAM(s) using geometry, not bam[2]
    size_t freeBlocks = 0;

    const size_t totalTracks = geom.sectorsPerTrack.size();
    const size_t bamCount = bamLocations.size();
    if (bamCount && totalTracks)
    {
        const size_t tracksPerBam = totalTracks / bamCount;
        size_t baseTrack = 1;

        for (size_t bamIndex = 0; bamIndex < bamCount; ++bamIndex)
        {
            const auto& loc = bamLocations[bamIndex];
            auto bamData = readSector(loc.track, loc.sector);

            size_t thisBamTracks = tracksPerBam;
            if (bamIndex + 1 == bamCount)
                thisBamTracks = totalTracks - (baseTrack - 1);

            for (size_t local = 1; local <= thisBamTracks; ++local)
            {
                freeBlocks += bamData[4 + (local - 1) * 4];
            }

            baseTrack += thisBamTracks;
        }
    }

    // Footer: "123 BLOCKS FREE.\r"
    char footer[32];
    snprintf(footer, sizeof(footer),
             "%3zu BLOCKS FREE.\r",
             freeBlocks);
    for (char ch : std::string(footer))
    {
        listing.push_back((uint8_t)ch);
    }

    return listing;
}

std::vector<uint8_t> CBMImage::loadFileByName(const std::string& name)
{
    uint8_t track = directoryStart.track;
    uint8_t sector = directoryStart.sector;

    while (track != 0)
    {
        std::vector<uint8_t> sectorData = readSector(track, sector);
        track = sectorData[0];
        sector = sectorData[1];

        for (int i = 0; i < 8; ++i)
        {
            size_t offset = 2 + (i * 32);
            uint8_t fileType = sectorData[offset + 0];

            if (fileType == 0x00) continue; // unused slot

            // Read raw PETSCII filename
            std::string filename;
            for (int j = 0; j < 16; ++j)
            {
                uint8_t c = sectorData[offset + 3 + j];
                filename += (c == 0xA0 ? ' ' : (char)c);
            }

            // Trim and match
            auto pos = filename.find_last_not_of(' ');
            if (pos == std::string::npos) filename.clear();
            else filename.erase(pos + 1);
            std::string normalized = filename;
            std::transform(normalized.begin(), normalized.end(), normalized.begin(), ::toupper);
            std::string search = name;
            std::transform(search.begin(), search.end(), search.begin(), ::toupper);

            if (normalized == search)
            {
                // Found the file — follow T/S chain
                uint8_t fileTrack  = sectorData[offset + 1];
                uint8_t fileSector = sectorData[offset + 2];
                std::vector<uint8_t> fileData;

                while (fileTrack != 0)
                {
                    std::vector<uint8_t> block = readSector(fileTrack, fileSector);
                    uint8_t nextTrack  = block[0];
                    uint8_t nextSector = block[1];

                    if (nextTrack == 0)
                    {
                        uint8_t lastByteCount = nextSector;
                        fileData.insert(fileData.end(), block.begin() + 2, block.begin() + 2 + lastByteCount);
                        break;
                    }
                    else
                    {
                        fileData.insert(fileData.end(), block.begin() + 2, block.end());
                        fileTrack = nextTrack;
                        fileSector = nextSector;
                    }
                }
                return fileData;
            }
        }
    }
    return {}; // File not found
}

bool CBMImage::writeFile(const std::string& fileName, const std::vector<uint8_t>& fileData)
{
    // Remove existing file if present
    deleteFile(fileName);

    // Allocate a sector chain for the data
    struct TrackSector { uint8_t track, sector; };
    std::vector<TrackSector> chain;
    size_t bytesRemaining = fileData.size();
    size_t dataOffset = 0;

    while (bytesRemaining > 0)
    {
        uint8_t t = 0, s = 0;
        if (!allocateSector(t, s))
        {
            // out of free sectors
            return false;
        }
        chain.push_back({t, s});
        bytesRemaining = (bytesRemaining > 254 ? bytesRemaining - 254 : 0);
    }

    // Write each data block with correct chaining
    for (size_t i = 0; i < chain.size(); ++i)
    {
        auto [t, s] = chain[i];
        std::vector<uint8_t> sectorBuf(SECTOR_SIZE, 0x00);

        // Pointer bytes
        if (i + 1 < chain.size())
        {
            sectorBuf[0] = chain[i+1].track;
            sectorBuf[1] = chain[i+1].sector;
        }
        else
        {
            // final block: byte 0 = 0, byte 1 = actual data length
            size_t chunk = std::min<size_t>(fileData.size() - dataOffset, 254);
            sectorBuf[1] = uint8_t(chunk);
        }

        // Copy payload
        size_t chunk = std::min<size_t>(fileData.size() - dataOffset, 254);
        std::copy_n(fileData.begin() + dataOffset, chunk, sectorBuf.begin() + 2);

        if (!writeSector(t, s, sectorBuf))
        {
            return false;
        }
        dataOffset += chunk;
    }

    // Find a free directory slot *anywhere* in the chain
    uint8_t dirTrack = directoryStart.track;
    uint8_t dirSector = directoryStart.sector;
    bool slotFound = false;
    size_t entryOff = 0;
    std::vector<uint8_t> dirBuf;

    while (dirTrack != 0 && !slotFound)
    {
        dirBuf = readSector(dirTrack, dirSector);
        for (int i = 0; i < 8; ++i)
        {
            size_t off = 2 + i * 32;
            if (dirBuf[off] == 0x00)
            {
                slotFound = true;
                entryOff  = off;
                break;
            }
        }
        if (!slotFound)
        {
            dirTrack  = dirBuf[0];
            dirSector = dirBuf[1];
        }
    }
    if (!slotFound)
    {
        // no directory space left
        return false;
    }

    // Populate the directory entry
    dirBuf[entryOff + 0] = 0x82;                     // PRG file, closed
    dirBuf[entryOff + 1] = chain.front().track;      // startTrack
    dirBuf[entryOff + 2] = chain.front().sector;     // startSector

    // PETSCII name (uppercase, pad with 0xA0)
    for (size_t i = 0; i < 16; ++i)
    {
        if (i < fileName.size())
        {
            unsigned char c = static_cast<unsigned char>(std::toupper(fileName[i]));
            dirBuf[entryOff + 3 + i] = asciiToPetscii(c);
        }
        else
        {
            dirBuf[entryOff + 3 + i] = 0xA0;
        }
    }

    // file length in sectors, little-endian at offsets 30/31
    uint16_t sectorCount = uint16_t(chain.size());
    dirBuf[entryOff + 30] = uint8_t(sectorCount & 0xFF);
    dirBuf[entryOff + 31] = uint8_t((sectorCount >> 8) & 0xFF);

    // Commit the updated directory sector
    if (!writeSector(dirTrack, dirSector, dirBuf)) {
        return false;
    }

    return true;
}

bool CBMImage::deleteFile(const std::string& fileName)
{
    // Locate the directory entry
    uint8_t dirTrack  = directoryStart.track;   // usually 18
    uint8_t dirSector = directoryStart.sector;  // usually 1
    uint8_t entryTrack = 0, entrySector = 0;
    size_t  entryOffset = 0;
    bool found = false;
    std::vector<uint8_t> dirBuf;

    // Normalize the input filename for case-insensitive comparison
    std::string normalized_fileName = fileName;
    std::transform(normalized_fileName.begin(), normalized_fileName.end(), normalized_fileName.begin(), ::toupper);

    while (dirTrack != 0 && !found)
    {
        dirBuf = readSector(dirTrack, dirSector);

        // scan all 8 entries in this sector
        for (int i = 0; i < 8; ++i)
        {
            size_t off = 2 + i * 32;
            uint8_t type = dirBuf[off];
            if (type == 0x00) continue;  // unused slot

            // extract and trim the PETSCII filename
            std::string fn;
            fn.reserve(16);
            for (int j = 0; j < 16; ++j)
            {
                fn += (dirBuf[off + 3 + j] == 0xA0 ? ' ' : char(dirBuf[off + 3 + j]));
            }

            auto last = fn.find_last_not_of(' ');

            if (last == std::string::npos)
            {
                fn.clear();
            }
            else
            {
                fn.erase(last + 1);
            }

            // Normalize the extracted filename to uppercase for comparison
            std::string normalized_fn = fn;
            std::transform(normalized_fn.begin(), normalized_fn.end(), normalized_fn.begin(), ::toupper);

            if (normalized_fn == normalized_fileName)
            {
                entryTrack  = dirTrack;
                entrySector = dirSector;
                entryOffset = off;
                found = true;
                break;
            }
        }

        if (!found)
        {
            // advance to the next directory sector
            dirTrack  = dirBuf[0];
            dirSector = dirBuf[1];
        }
    }

    if (!found)
    {
        return false;  // file not found
    }

    // Follow and free each data block in the chain
    uint8_t track  = dirBuf[entryOffset + 1];
    uint8_t sector = dirBuf[entryOffset + 2];
    while (track != 0)
    {
        auto data = readSector(track, sector);
        uint8_t nextTrack  = data[0];
        uint8_t nextSector = data[1];

        freeSector(track, sector);

        track  = nextTrack;
        sector = nextSector;
    }

    // Clear out the directory slot re-read the sector only if it wasn’t the last one we loaded
    if (entryTrack != dirTrack || entrySector != dirSector)
    {
        dirBuf = readSector(entryTrack, entrySector);
    }

    dirBuf[entryOffset + 0] = 0x00;  // clear fileType
    dirBuf[entryOffset + 1] = 0x00;  // clear startTrack
    dirBuf[entryOffset + 2] = 0x00;  // clear startSector
    std::fill(dirBuf.begin() + entryOffset + 3, dirBuf.begin() + entryOffset + 3 + 16, uint8_t(0xA0)); // PETSCII spaces
    writeSector(entryTrack, entrySector, dirBuf);
    return true;
}

bool CBMImage::renameFile(const std::string& oldName, const std::string& newName)
{
    // Find the directory entry for oldName
    uint8_t dirTrack = directoryStart.track;   // usually 18
    uint8_t dirSector = directoryStart.sector;  // usually 1
    uint8_t entryTrack = 0, entrySector = 0;
    size_t entryOffset= 0;
    bool found = false;
    std::vector<uint8_t> dirBuf;

    // Normalize the oldName for case-insensitive search
    std::string normalized_oldName = oldName;
    std::transform(normalized_oldName.begin(), normalized_oldName.end(), normalized_oldName.begin(), ::toupper);

    while (dirTrack != 0 && !found)
    {
        dirBuf = readSector(dirTrack, dirSector);

        for (int i = 0; i < 8; ++i)
        {
            size_t off = 2 + i * 32;
            uint8_t type = dirBuf[off];
            if (type == 0x00) continue;

            // extract and trim filename
            std::string fn;
            fn.reserve(16);

            for (int j = 0; j < 16; ++j)
            {
                uint8_t c = dirBuf[off + 3 + j];
                fn += (c == 0xA0 ? ' ' : char(c));
            }

            auto last = fn.find_last_not_of(' ');

            if (last == std::string::npos)
            {
                fn.clear();
            }
            else
            {
                fn.erase(last + 1);
            }

            // Normalize the extracted filename to uppercase for comparison
            std::string normalized_fn = fn;
            std::transform(normalized_fn.begin(), normalized_fn.end(), normalized_fn.begin(), ::toupper);

            if (normalized_fn == normalized_oldName)
            {
                entryTrack   = dirTrack;
                entrySector  = dirSector;
                entryOffset  = off;
                found = true;
                break;
            }
        }

        if (!found)
        {
            // hop to next directory sector
            dirTrack  = dirBuf[0];
            dirSector = dirBuf[1];
        }
    }

    if (!found)
    {
        // nothing to rename
        return false;
    }

    // Clear the existing filename field with PETSCII spaces (0xA0)
    std::fill(dirBuf.begin() + entryOffset + 3, dirBuf.begin() + entryOffset + 3 + 16, uint8_t(0xA0));

    // Overwrite the 16 byte filename field in dirBuf with newName in PETSCII, pad with 0xA0
    for (int i = 0; i < 16; ++i)
    {
        if (i < int(newName.size()))
        {
            unsigned char asc = static_cast<unsigned char>(newName[i]);
            asc = std::toupper(asc);
            dirBuf[entryOffset + 3 + i] = asciiToPetscii(asc);
        }
        else
        {
            dirBuf[entryOffset + 3 + i] = 0xA0;
        }
    }

    // Commit back to disk
    writeSector(entryTrack, entrySector, dirBuf);
    return true;
}

bool CBMImage::copyFile(const std::string& srcName, const std::string& destName)
{
    auto data = loadFileByName(srcName);
    if (data.empty())
    {
        // File does not exist
        return false;
    }

    bool ok = writeFile(destName, data);
    if (!ok)
    {
        // Hit an error, could be out of space
        return false;
    }
    else
    {
        return true;
    }
}

bool CBMImage::formatDisk(const std::string& volumeName, const std::string& volumeID)
{
    size_t totalSectors = 0;
    for(auto s : geom.sectorsPerTrack)
    {
        totalSectors += s;
    }

    // Resize and clear the image
    fileImageBuffer.assign(totalSectors * SECTOR_SIZE, 0x00);

    // Build each BAM sector
    size_t numBAMS = bamLocations.size();
    size_t tracksPerBAM = geom.sectorsPerTrack.size() / numBAMS;

    for (size_t side = 0; side < numBAMS; ++side)
    {
        auto [bamTrack, bamSector] = bamLocations[side];
        std::vector<uint8_t> bam(SECTOR_SIZE, 0x00);

        // Next directory track
        bam[0] = directoryStart.track;
        bam[1] = directoryStart.sector;
        bam[2] = static_cast<uint8_t>(tracksPerBAM);

        // For each track in this side of the disk: free count + per sector bitmask
        for (uint8_t t = 1; t <= tracksPerBAM; t++)
        {
            size_t globalTrack = side * tracksPerBAM + (t - 1);
            uint8_t freeCount = geom.sectorsPerTrack[globalTrack];
            size_t entryOffset = 4 + (t -1) * 4;

            // Free sector count
            bam[entryOffset] = freeCount;

            // Mark all sectors free
            for (uint8_t s = 0; s < freeCount; s++)
            {
                size_t byteOffset = entryOffset + 1 + (s / 8);
                bam[byteOffset] |= uint8_t(1 << (s % 8));
            }
        }

        // Volume name at 0x90 to 0x9F padded with 0xA0 to 16 characters in PETSCII
        for (size_t i = 0; i < 16; i++)
        {
            bam[0x90 + i] = (i < volumeName.size()) ? asciiToPetscii(volumeName[i]) : 0xA0;
        }

        // Volume ID at 0xA2 to 0xA3
        bam[0xA2] = asciiToPetscii((volumeID[0]));
        bam[0xA3] = asciiToPetscii((volumeID[1]));
        // DOS signature 2A at 0xA5 to 0xA7
        bam[0xA5] = '2';
        bam[0xA6] = 'A';
        bam[0xA7] = ' ';

        // Write the bam sector out to the image
        if (!writeSector(bamTrack, bamSector, bam))
        {
            return false;
        }
    }

    // Write out a blank directory start sector so LOAD"$",8 will return 0 files
    std::vector<uint8_t> directoryBuffer(SECTOR_SIZE, 0x00);
    directoryBuffer[0] = 0;
    directoryBuffer[1] = 0xFF;
    if (!writeSector(directoryStart.track, directoryStart.sector, directoryBuffer))
    {
        return false;
    }
    return true;
}

bool CBMImage::validateDirectory()
{
    return true;
}

bool CBMImage::isValidPETSCII(uint8_t c)
{
    return (c >= 0x20 && c <= 0x5F) || (c >= 0xA0 && c <= 0xDF);
}

bool CBMImage::validateHeader()
{
    // Track 18, sector 0 is the BAM + header for 1541-style disks
    size_t offset;
    try
    {
        offset = computeOffset(18, 0);
    }
    catch (const std::out_of_range&)
    {
        return false;
    }

    // We need at least up through $A7 in this sector
    if (offset + 0xA7 >= fileImageBuffer.size())
    {
        return false; // image too small for header check
    }

    uint8_t dosType0 = fileImageBuffer[offset + 0xA5]; // first DOS type char
    uint8_t dosType1 = fileImageBuffer[offset + 0xA6]; // second DOS type char

    // Accept common DOS types:
    //  "2A" = 1541 / 5.25" (standard D64)
    //  "2B","2C","2D","3D" etc. exist on other drives, so don't be overly strict.
    auto isValidType = [](uint8_t c0, uint8_t c1) -> bool
    {
        if (c0 != '2' && c0 != '3')
            return false;

        switch (c1)
        {
            case 'A': // 2A: standard 1541/1571 5.25"
            case 'B':
            case 'C':
            case 'D': // 3D etc. used by other variants
                return true;
            default:
                return false;
        }
    };

    if (!isValidType(dosType0, dosType1))
    {
        return false;
    }

    return true;
}

bool CBMImage::validateDiskNameAndID()
{
    size_t offset = computeOffset(18, 0);

    // Disk name check (0x90–0x9F)
    for (size_t i = 0; i < 16; i++)
    {
        uint8_t c = fileImageBuffer[offset + 0x90 + i];
        if (!isValidPETSCII(c))
        {
            return false;
        }
    }

    // Disk ID check (0xA2–0xA3)
    for (size_t i = 0; i < 2; i++)
    {
        uint8_t c = fileImageBuffer[offset + 0xA2 + i];
        if (!isValidPETSCII(c))
        {
            return false;
        }
    }

    // Check passed
    return true;
}

bool CBMImage::validateDirectoryChain()
{
    uint8_t track  = directoryStart.track;
    uint8_t sector = directoryStart.sector;

    std::set<std::pair<uint8_t, uint8_t>> visited;

    while (track != 0)
    {
        // Detect loops
        if (visited.count({track, sector}))
        {
            return false;
        }
        visited.insert({track, sector});

        // Bounds for the *current* directory sector
        size_t offset = computeOffset(track, sector);
        if (offset + SECTOR_SIZE > fileImageBuffer.size())
        {
            return false;
        }

        // Read the *next* link in the chain
        uint8_t nextTrack  = fileImageBuffer[offset];
        uint8_t nextSector = fileImageBuffer[offset + 1];

        // If this is not the end-of-chain marker, validate it
        if (nextTrack != 0)
        {
            // Check track in range of the loaded geometry
            if (nextTrack < 1 ||
                nextTrack > geom.sectorsPerTrack.size())
            {
                return false;
            }

            // Check sector in range for that track
            if (nextSector >= getSectorsForTrack(nextTrack))
            {
                return false;
            }
        }

        // Advance
        track  = nextTrack;
        sector = nextSector;
    }

    // All directory sectors walked with sane links
    return true;
}

bool CBMImage::validateDiskImage()
{
    return validateHeader() && validateDiskNameAndID() && validateDirectoryChain();
}

uint8_t CBMImage::asciiToPetscii(unsigned char asciiChar)
{
    std::vector<unsigned char> ascii_to_petscii_table =
            {
                0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x14,0x20,0x0d,0x11,0x93,0x0a,0x0e,0x0f,
                0x10,0x0b,0x12,0x13,0x08,0x15,0x16,0x17,0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,
                0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,0x29,0x2a,0x2b,0x2c,0x2d,0x2e,0x2f,
                0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x3b,0x3c,0x3d,0x3e,0x3f,
                0x40,0xc1,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xcb,0xcc,0xcd,0xce,0xcf,
                0xd0,0xd1,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0x5b,0x5c,0x5d,0x5e,0xa4,
                0xc0,0x41,0x42,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,0x4b,0x4c,0x4d,0x4e,0x4f,
                0x50,0x51,0x52,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0xdb,0xdc,0xdd,0xde,0xdf,
                0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8a,0x8b,0x8c,0x8d,0x8e,0x8f,
                0x90,0x91,0x92,0x0c,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0x9b,0x9c,0x9d,0x9e,0x9f,
                0xa0,0xa1,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xab,0xac,0xad,0xae,0xaf,
                0xb0,0xb1,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xbb,0xbc,0xbd,0xbe,0xbf,
                0x60,0x61,0x62,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x6b,0x6c,0x6d,0x6e,0x6f,
                0x70,0x71,0x72,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x7b,0x7c,0x7d,0x7e,0x7f,
                0xe0,0xe1,0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xeb,0xec,0xed,0xee,0xef,
                0xf0,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa,0xfb,0xfc,0xfd,0xfe,0xff
            };
    if (asciiChar < ascii_to_petscii_table.size())
    {
        return ascii_to_petscii_table[asciiChar];
    }
    else
    {
        return asciiChar;
    }
}

bool CBMImage::allocateSector(uint8_t& outTrack, uint8_t& outSector)
{
    const size_t totalTracks = geom.sectorsPerTrack.size();
    const size_t bamCount = bamLocations.size();
    if (bamCount == 0 || totalTracks == 0) return false;

    const size_t tracksPerBam = totalTracks / bamCount;
    if (tracksPerBam == 0) return false;

    size_t baseTrack = 1;

    for (size_t bamIndex = 0; bamIndex < bamCount; ++bamIndex)
    {
        const auto& loc = bamLocations[bamIndex];
        auto bam = readSector(loc.track, loc.sector);

        size_t thisBamTracks = tracksPerBam;
        if (bamIndex + 1 == bamCount)
            thisBamTracks = totalTracks - (baseTrack - 1);

        for (size_t local = 1; local <= thisBamTracks; ++local)
        {
            const uint8_t track = static_cast<uint8_t>(baseTrack + (local - 1));
            const size_t entry = 4 + (local - 1) * 4;

            if (bam[entry] == 0) continue;

            for (uint8_t byteOff = 1; byteOff <= 3; ++byteOff)
            {
                uint8_t mask = bam[entry + byteOff];
                if (!mask) continue;

                for (uint8_t bit = 0; bit < 8; ++bit)
                {
                    if ((mask & (1u << bit)) == 0) continue;

                    const uint8_t sector = static_cast<uint8_t>((byteOff - 1) * 8 + bit);
                    if (sector >= getSectorsForTrack(track)) continue;

                    // Don’t allocate BAM sectors or your first directory sector
                    if (track == directoryStart.track && sector == directoryStart.sector)
                        continue;

                    bool isBamSector = false;
                    for (const auto& r : bamLocations)
                    {
                        if (track == r.track && sector == r.sector) { isBamSector = true; break; }
                    }
                    if (isBamSector) continue;

                    // Allocate it
                    bam[entry]--;
                    bam[entry + byteOff] &= static_cast<uint8_t>(~(1u << bit));
                    writeSector(loc.track, loc.sector, bam);

                    outTrack = track;
                    outSector = sector; // 0-based
                    return true;
                }
            }
        }

        baseTrack += thisBamTracks;
    }

    return false;
}

void CBMImage::freeSector(uint8_t track, uint8_t sector)
{
    const size_t totalTracks = geom.sectorsPerTrack.size();
    const size_t bamCount = bamLocations.size();
    if (bamCount == 0 || totalTracks == 0) return;

    if (track < 1 || track > totalTracks) return;
    if (sector >= getSectorsForTrack(track)) return;

    // Don’t free BAM or first directory sector
    if (track == directoryStart.track && sector == directoryStart.sector) return;
    for (const auto& r : bamLocations)
        if (track == r.track && sector == r.sector) return;

    const size_t tracksPerBam = totalTracks / bamCount;
    if (tracksPerBam == 0) return;

    size_t baseTrack = 1;

    for (size_t bamIndex = 0; bamIndex < bamCount; ++bamIndex)
    {
        size_t thisBamTracks = tracksPerBam;
        if (bamIndex + 1 == bamCount)
            thisBamTracks = totalTracks - (baseTrack - 1);

        if (track >= baseTrack && track < baseTrack + thisBamTracks)
        {
            const auto& loc = bamLocations[bamIndex];
            auto bam = readSector(loc.track, loc.sector);

            const size_t local = static_cast<size_t>(track - baseTrack + 1); // 1-based
            const size_t entry = 4 + (local - 1) * 4;

            const uint8_t byteOff = static_cast<uint8_t>(1 + (sector / 8));
            const uint8_t bit     = static_cast<uint8_t>(sector % 8);
            const uint8_t bitMask = static_cast<uint8_t>(1u << bit);

            // Only change if it wasn't already free
            if ((bam[entry + byteOff] & bitMask) == 0)
            {
                bam[entry]++;
                bam[entry + byteOff] |= bitMask;
                writeSector(loc.track, loc.sector, bam);
            }
            return;
        }

        baseTrack += thisBamTracks;
    }
}
