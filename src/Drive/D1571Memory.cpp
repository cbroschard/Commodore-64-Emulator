// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "D1571Memory.h"

D1571Memory::D1571Memory() :
    logger(nullptr),
    setLogging(false)
{
    D1571RAM.resize(RAM_SIZE,0);
    D1571ROM.resize(ROM_SIZE,0);
}

D1571Memory::~D1571Memory()
{

}

void D1571Memory::reset()
{
    // Reset all RAM to 0's
    std::fill(D1571RAM.begin(), D1571RAM.end(), 0x00);

    // Reset all chips
    via1.reset();
    via2.reset();
    cia.reset();
}

uint8_t D1571Memory::read(uint16_t address)
{
    if (address >= RAM_START && address <= RAM_END)
    {
        return D1571RAM[address - RAM_START];
    }
    else if (address >= ROM_START && address <= ROM_END)
    {
        return D1571ROM[address - ROM_START];
    }
    else if (address >= VIA1_START && address <= VIA2_END)
    {
        return via1.readRegister((address - VIA1_START) & 0x0F);
    }
    else if (address >= VIA2_START && address <= VIA2_END)
    {
        return via2.readRegister((address - VIA2_START) & 0x0F);
    }
    else if (address >= CIA_START && address <= CIA_END)
    {
        return cia.readRegister((address - CIA_START) & 0x0F);
    }
    return 0xFF; // open bus
}

void D1571Memory::write(uint16_t address, uint8_t value)
{
    if (address >= RAM_START && address <= RAM_END)
    {
        D1571RAM[address] = value;
    }
    else if (address >= VIA1_START && address <= VIA1_END)
    {
        via1.writeRegister(address, value);
    }
    else if (address >= VIA2_START && address <= VIA2_END)
    {
        via2.writeRegister(address, value);
    }
    else if (address >= CIA_START && address <= CIA_END)
    {
        cia.writeRegister(address, value);
    }
}

bool D1571Memory::loadROM(const std::string& filename, std::vector<uint8_t>& targetBuffer, size_t expectedSize, const std::string& romName)
{
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
        if (logger && setLogging)
        {
            logger->WriteLog("Unable to open " + romName + " ROM file: " + filename);
        }
        return false;
    }

    std::streamsize fileSize = file.tellg();
    if (static_cast<size_t>(fileSize) != expectedSize)
    {
        if (logger && setLogging)
        {
        logger->WriteLog("Error: " + romName + " ROM file is not correct size! Expected " +
                         std::to_string(expectedSize) + " bytes, got " + std::to_string(fileSize) + " bytes.");
        }
        return false;
    }

    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(targetBuffer.data()), expectedSize))
    {
        if (logger && setLogging)
        {
            logger->WriteLog("Error: Failed to read " + romName + " ROM file: " + filename);
        }
        return false;
    }

    file.close();
    return true;
}
