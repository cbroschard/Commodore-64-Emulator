// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Memory.h"

Memory::Memory()
{
    mem.resize(MAX_MEMORY,0);
    basicROM.resize(BASIC_ROM_SIZE,0);
    kernalROM.resize(KERNAL_ROM_SIZE,0);
    charROM.resize(CHAR_ROM_SIZE,0);
    colorRAM.resize(COLOR_RAM_SIZE,0);
}

Memory::~Memory() = default;

void Memory::saveState(StateWriter& wrtr) const
{
    // MEM0 = "Core"
    wrtr.beginChunk("MEM0");
    wrtr.writeU32(1); //version

    // Dump main memory
    wrtr.writeVectorU8(mem);

    // Dump Color RAM
    wrtr.writeVectorU8(colorRAM);

    // End the chunk for CIA1
    wrtr.endChunk();
}

bool Memory::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "MEM0", 4) == 0)
    {
        rdr.enterChunkPayload(chunk);

        uint32_t ver = 0;
        if (!rdr.readU32(ver))                                              { rdr.exitChunkPayload(chunk); return false; }
        if (ver != 1)                                                       { rdr.exitChunkPayload(chunk); return false; }

        // Load Main memory
        if (!rdr.readVectorU8(mem))                                         { rdr.exitChunkPayload(chunk); return false; }

        // Load Color RAM
        if (!rdr.readVectorU8(colorRAM))                                    { rdr.exitChunkPayload(chunk); return false; }

        rdr.exitChunkPayload(chunk);
        return true;
    }

    // Not our chunk
    return false;
}

uint8_t Memory::readRAM(uint16_t address) const
{
    return mem[address];
}

uint8_t Memory::readKernalROM(uint16_t address) const
{
    return kernalROM[address];
}

uint8_t Memory::readBASICROM(uint16_t address) const
{
    return basicROM[address];
}

uint8_t Memory::readCharROM(uint16_t address) const
{
    return charROM[address];
}

uint8_t Memory::readColorRAM(uint16_t address) const
{
    return colorRAM[address] & 0x0F;
}

void Memory::write16(uint16_t address, uint16_t value)
{
    writeRAM(address, static_cast<uint8_t>(value & 0xFF));
    writeRAM(static_cast<uint16_t>(address + 1), static_cast<uint8_t>((value >> 8) & 0xFF));
}

void Memory::writeRAM(uint16_t address, uint8_t value)
{
    mem[address] = value;
}

void Memory::writeColorRAM(uint16_t address, uint8_t value)
{
    colorRAM[address] = value & 0x0F;
}

void Memory::writeDirect(uint16_t address, uint8_t value)
{
    if (address < MAX_MEMORY)
        mem[address] = value;
}

bool Memory::load_ROM(const std::string& filename, std::vector<uint8_t>& targetBuffer, size_t expectedSize, const std::string& romName)
{
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        return false;

    std::streamsize fileSize = file.tellg();
    if (static_cast<size_t>(fileSize) != expectedSize)
        return false;

    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(targetBuffer.data()), expectedSize))
        return false;

    file.close();
    return true;
}

bool Memory::Initialize(const std::string& basic, const std::string& kernal, const std::string& character)
{
    // Initialize RAM to 0
    for (size_t i = 0; i < mem.size(); ++i)
    {
        mem[i] = (i & 0x40) ? 0xFF : 0x00;
    }

    // Load each ROM and check for successful load
    if (!load_ROM(basic, basicROM, 0x2000, "BASIC") || !load_ROM(kernal, kernalROM, 0x2000, "Kernal") || !load_ROM(character, charROM, 0x1000, "CHAR"))
    {
        return false;
    }
    else
    {
        return true;
    }
}
