// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Tape/T64.h"

T64::T64() :
    fileLoaded(false),
    prgStart(0),
    prgEnd(0),
    prgPtr(0),
    prgLen(0),
    curByte(0)
{

}

T64::~T64() = default;

bool T64::loadTape(const std::string& filePath, VideoMode mode)
{
    // Attempt to load the file
    if (!loadFile(filePath, tapeData))
    {
        return false;
    }

    // Validate size
    if (tapeData.size() < sizeof(header))
    {
        throw std::runtime_error("Error: File too small to contain a valid header.");
    }

    // Copy header bytes from tapeData into header
    std::memcpy(&header, tapeData.data(), sizeof(header));

    if (!validateHeader())
    {
        return false;
    }
    int entryOffset = 0x40;

    // Validate there's at least one entry
    bool found = false;
    for (int i = 0; i < header.maxEntries; ++i)
    {
        const uint8_t* entry = &tapeData[entryOffset + i * 32];
        if (entry[0] == 1) { // Used entry
            prgStart = entry[0x02] | (entry[0x03] << 8);
            prgEnd   = entry[0x04] | (entry[0x05] << 8);
            prgPtr   = entry[0x08] | (entry[0x09] << 8) | (entry[0x0A] << 8) | (entry[0x0B] << 8);
            prgLen   = prgEnd - prgStart + 1;
            found = true;
            break;
        }
    }

    if (!found)
    {
        std::cerr << "No valid file entry in T64!\n";
        return false;
    }
    fileLoaded = true;
    return true;
}

void T64::simulateLoading()
{

}

bool T64::validateHeader()
{
   // First check the signature to ensure it's really C64 tape file, work with 2 known types
    static const char t64Sig1[] = "C64S tape image file";
    static const char t64Sig2[] = "C64 tape image file";
    bool found = false;

    for (int i = 0; i <= 32 - 16; ++i)
    {
        if (std::strncmp(header.headerID + i, t64Sig1, strlen(t64Sig1)) == 0 ||
                std::strncmp(header.headerID + i, t64Sig2, strlen(t64Sig2)) == 0)
        {
            found = true;
            break;
        }
    }

    if (!found)
    {
        std::cerr << "Error: Tape file is not a C64 tape!" << std::endl;
        return false;
    }

    if (header.version != 0x0100 && header.version != 0x0101 && header.version != 0x0200 && header.version != 0x2020)
    {
        std::cerr << "Error: Invalid tape version!" << std::endl;
        return false;
    }
    return true;
}

void T64::rewind()
{
    // No need to implement
}

bool T64::loadFile(const std::string& path, std::vector<uint8_t>& buffer)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
        std::cerr << "Failed to open TAPE file: " << path << std::endl;
        return false;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    buffer.resize(size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size))
    {
        std::cerr << "Failed to read TAPE file: " << path << std::endl;
        return false;
    }

    #ifdef Debug
    std::cout << "Loaded TAPE file: " << path << " (" << size << " bytes)" << std::endl;
    #endif // Debug
    return true;
}

bool T64::currentBit() const
{
    return 0; // Fix to actually return current level when implemented
}

bool T64::hasLoadedFile() const
{
    return fileLoaded;
}

uint16_t T64::getPrgStart() const
{
    return prgStart;
}

uint16_t T64::getPrgEnd() const
{
    return prgEnd;
}

const uint8_t* T64::getPrgData() const
{
    return tapeData.data() + prgPtr;
}

bool T64::isT64() const
{
    return true;
}
