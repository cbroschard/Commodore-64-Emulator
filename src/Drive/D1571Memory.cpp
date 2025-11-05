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

D1571Memory::~D1571Memory() = default;

void D1571Memory::reset()
{
    // Reset all RAM to 0's
    std::fill(D1571RAM.begin(), D1571RAM.end(), 0x00);

    // Reset all chips
    via1.reset();
    via2.reset();
    cia.reset();
    fdc.reset();
}

void D1571Memory::tick()
{
    via1.tick();
    via2.tick();
    cia.tick();
    fdc.tick();
}

bool D1571Memory::initialize(const std::string& fileName)
{
    // Clear RAM/ROM
    std::fill(D1571RAM.begin(), D1571RAM.end(), 0x00);
    std::fill(D1571ROM.begin(), D1571ROM.end(), 0x00);

    // Attempt to load passed in ROM file
    if (!loadROM(fileName)) return false;

    // All good
    return true;
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
    else if (address >= VIA1_START && address <= VIA1_END)
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
    else if (address >= FDC_START && address <= FDC_END)
    {
        return fdc.readRegister(address - FDC_START);
    }
    return 0xFF; // open bus
}

void D1571Memory::write(uint16_t address, uint8_t value)
{
    if (address >= RAM_START && address <= RAM_END)
    {
        D1571RAM[address - RAM_START] = value;
    }
    else if (address >= VIA1_START && address <= VIA1_END)
    {
        via1.writeRegister(address - VIA1_START, value);
    }
    else if (address >= VIA2_START && address <= VIA2_END)
    {
        via2.writeRegister(address - VIA2_START, value);
    }
    else if (address >= CIA_START && address <= CIA_END)
    {
        cia.writeRegister(address - CIA_START, value);
    }
    else if (address >= FDC_START && address <= FDC_END)
    {
        fdc.writeRegister(address - FDC_START, value);
    }
}

bool D1571Memory::loadROM(const std::string& filename)
{
    return true;
}
