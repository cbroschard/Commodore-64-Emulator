// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Drive/D1541Memory.h"

D1541Memory::D1541Memory()
{
    // Initialize Memory to all 0's
    D1541RAM.resize(RAM_SIZE,0);
    D1541ROM1.resize(ROM_SIZE,0);
    D1541ROM2.resize(ROM_SIZE,0);
}

D1541Memory::~D1541Memory() = default;

void D1541Memory::attachLoggingInstance(Logging* logger)
{
    this->logger = logger;
}

void D1541Memory::tick()
{
    via1.tick(1);
    via2.tick(1);

    // IRQ check from VIA chips
    if ((via1.readRegister(0x0D) & 0x80) || (via2.readRegister(0x0D) & 0x80))
    {
        driveIRQ.raiseIRQ(IRQLine::D1541_IRQ);
    }
    else
    {
        driveIRQ.clearIRQ(IRQLine::D1541_IRQ);
    }
}

void D1541Memory::reset()
{
    // Reset all RAM to 0's
    std::fill(D1541RAM.begin(), D1541RAM.end(), 0x00);

    // Reset both VIA chips
    via1.reset();
    via2.reset();
}

uint8_t D1541Memory::read(uint16_t address)
{
    if (address >= D1541_RAM_START && address <= D1541_RAM_END)
    {
        return D1541RAM[address - D1541_RAM_START];
    }
    else if (address >= VIA1_START && address <= VIA1_END)
    {
        return via1.readRegister((address - VIA1_START) & 0x0F);
    }
    else if (address >= VIA2_START && address <= VIA2_END)
    {
        return via2.readRegister((address - VIA2_START) & 0x0F);
    }
    else if (address >= ROM1_START && address <= ROM1_END)
    {
        return D1541ROM1[address - ROM1_START];
    }
    else if (address >= ROM2_START && address <= ROM2_END)
    {
        return D1541ROM2[address - ROM2_START];
    }
    else
    {
        // default
        return 0xFF;
    }
}

void D1541Memory::write(uint16_t address, uint8_t value)
{
    if (address >= D1541_RAM_START && address <= D1541_RAM_END)
    {
        D1541RAM[address - D1541_RAM_START] = value;
    }
    else if (address >= VIA1_START && address <= VIA1_END)
    {
        via1.writeRegister(address - VIA1_START, value);
    }
    else if (address >= VIA2_START && address <= VIA2_END)
    {
        via2.writeRegister(address - VIA2_START, value);
    }
    else
    {
        if (logger)
        {
            logger->WriteLog("Attempt to write to invalid 1541 address: " + std::to_string(address));
        }
    }
}

bool D1541Memory::initialize(const std::string& D1541LoROM, const std::string& D1541HiROM)
{
    // Initialize Drive RAM to 0's
    std::fill(D1541RAM.begin(), D1541RAM.end(), 0x00);

    // Initialize both ROMS to 0's before loading
    std::fill(D1541ROM1.begin(), D1541ROM1.end(), 0x00);
    std::fill(D1541ROM2.begin(), D1541ROM2.end(), 0x00);

    // Try to load in ROM files passed in via config
    if (!loadROM(D1541LoROM, D1541ROM1, ROM_SIZE, "1541 ROM Lo") || !loadROM(D1541HiROM, D1541ROM2, ROM_SIZE, "1541 ROM Hi"))
    {
        return false;
    }

    return true;
}

IRQLine* D1541Memory::getIRQLine()
{
    return &driveIRQ;
}

D1541VIA& D1541Memory::getVIA1()
{
    return via1;
}

D1541VIA& D1541Memory::getVIA2()
{
    return via2;
}

bool D1541Memory::loadROM(const std::string& filename, std::vector<uint8_t>& targetBuffer, size_t expectedSize, const std::string& romName)
{
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
        if (logger)
        {
            logger->WriteLog("Unable to open " + romName + " ROM file: " + filename);
        }
        return false;
    }

    std::streamsize fileSize = file.tellg();
    if (static_cast<size_t>(fileSize) != expectedSize)
    {
        if (logger)
        {
        logger->WriteLog("Error: " + romName + " ROM file is not correct size! Expected " +
                         std::to_string(expectedSize) + " bytes, got " + std::to_string(fileSize) + " bytes.");
        }
        return false;
    }

    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(targetBuffer.data()), expectedSize))
    {
        if (logger)
        {
            logger->WriteLog("Error: Failed to read " + romName + " ROM file: " + filename);
        }
        return false;
    }

    file.close();
    return true;
}
