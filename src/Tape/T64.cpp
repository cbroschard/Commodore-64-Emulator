// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Tape/T64.h"

T64::T64()
{
    reset();
}

T64::~T64() = default;

void T64::reset()
{
    entries.clear();

    selectedEntry = 0;
    fileLoaded = false;

    prgStart = 0;
    prgEnd = 0;
    prgPtr = 0;
    prgLen = 0;

    tapeData.clear();
    std::memset(&header, 0, sizeof(header));
}

bool T64::loadTape(const std::string& filePath, VideoMode mode)
{
    (void)mode;

    reset();

    // Attempt to load the file
    if (!loadFile(filePath, tapeData))
        return false;

    // Validate size
    if (tapeData.size() < sizeof(header))
        throw std::runtime_error("Error: File too small to contain a valid header.");

    // Copy header bytes from tapeData into header
    std::memcpy(&header, tapeData.data(), sizeof(header));

    if (!validateHeader())
        return false;

    const size_t directoryStart = 0x40;
    const size_t directorySize = static_cast<size_t>(header.maxEntries) * 32;

    if (directoryStart + directorySize > tapeData.size())
    {
        std::cerr << "Error: T64 directory exceeds file size!\n";
        return false;
    }

    for (size_t i = 0; i < header.maxEntries; ++i)
    {
        const size_t offset = directoryStart + (i * 32);
        const uint8_t* entry = &tapeData[offset];

        // VICE only meaningfully supports entry type 1 for normal T64 files.
        if (entry[0] != 1)
            continue;

        T64Entry parsedEntry;

        parsedEntry.entryType = entry[0];
        parsedEntry.fileType  = entry[1];

        parsedEntry.startAddress = static_cast<uint16_t>(entry[0x02]) | (static_cast<uint16_t>(entry[0x03]) << 8);

        parsedEntry.endAddress = static_cast<uint16_t>(entry[0x04]) | (static_cast<uint16_t>(entry[0x05]) << 8);

        parsedEntry.dataOffset =
              static_cast<uint32_t>(entry[0x08])
            | (static_cast<uint32_t>(entry[0x09]) << 8)
            | (static_cast<uint32_t>(entry[0x0A]) << 16)
            | (static_cast<uint32_t>(entry[0x0B]) << 24);

        if (parsedEntry.endAddress < parsedEntry.startAddress)
        {
            std::cerr << "Warning: Invalid T64 entry address range.\n";
            continue;
        }

        parsedEntry.dataLength = static_cast<uint32_t>(parsedEntry.endAddress) - static_cast<uint32_t>(parsedEntry.startAddress);

        if (parsedEntry.dataOffset >= tapeData.size() || parsedEntry.dataLength > tapeData.size() - parsedEntry.dataOffset)
        {
            std::cerr << "Warning: T64 entry data exceeds file size.\n";
            continue;
        }

        // Filename occupies bytes $10-$1F.
        parsedEntry.filename.clear();

        for (size_t c = 0; c < 16; ++c)
        {
            const uint8_t ch = entry[0x10 + c];

            if (ch == 0)
                break;

            parsedEntry.filename.push_back(static_cast<char>(ch));
        }

        while (!parsedEntry.filename.empty())
        {
            const uint8_t ch = static_cast<uint8_t>(parsedEntry.filename.back());

            if (ch != 0x20 && ch != 0xA0)
                break;

            parsedEntry.filename.pop_back();
        }

        entries.push_back(std::move(parsedEntry));
    }

    if (entries.empty())
    {
        std::cerr << "No valid file entries in T64!\n";
        return false;
    }

    if (!selectEntry(0))
        return false;

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
    if (!fileLoaded || prgPtr >= tapeData.size())
        return nullptr;

    return tapeData.data() + prgPtr;
}

bool T64::isT64() const
{
    return true;
}

bool T64::selectEntry(size_t index)
{
    if (index >= entries.size())
        return false;

    const auto& entry = entries[index];

    selectedEntry = index;
    prgStart = entry.startAddress;
    prgEnd = entry.endAddress;
    prgPtr = entry.dataOffset;
    prgLen = entry.dataLength;

    return true;
}

const std::vector<T64::T64Entry>& T64::getEntries() const
{
    return entries;
}

size_t T64::getSelectedEntry() const
{
    return selectedEntry;
}
