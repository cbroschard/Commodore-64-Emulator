// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/KCSPowerMapper.h"
#include "Memory.h"

KCSPowerMapper::KCSPowerMapper() :
    processor(nullptr)
{

}

KCSPowerMapper::~KCSPowerMapper() = default;

void KCSPowerMapper::saveState(StateWriter& wrtr) const
{
    // No-op - cartridge saves RAMData and line states
}

bool KCSPowerMapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    // No-op
    return true;
}

uint8_t KCSPowerMapper::read(uint16_t address)
{
    // IO1: $DE00-$DEFF
    if (address >= 0xDE00 && address <= 0xDEFF)
    {
        const bool a1 = (address & 0x02) != 0;
        if (!a1)
        {
            // Set 8K mode
            cart->setGameLine(true);
            cart->setExROMLine(false);
        }
        else
        {
            // "RAM config - Disable cartridge
            cart->setGameLine(true);
            cart->setExROMLine(true);
        }

        uint16_t offset = 0x1E00 + (address & 0x00FF);
        return mem->readCartridge(offset, cartLocation::LO);
    }

    // IO2: $DF00-$DFFF
    if (address >= 0xDF00 && address <= 0xDFFF)
    {
        // $DF00-$DF7F = 128 bytes cartridge RAM
        if ((address & 0x80) == 0)
        {
            if (cart && cart->hasCartridgeRAM())
                return cart->readRAM(address & 0x7F);
            return 0xFF;
        }

        // $DF80-$DFFF = open area, but bits 7/6 reflect EXROM/GAME.
        // bit7 = EXROM line level, bit6 = GAME line level, lower 6 bits = open bus.
        uint8_t open = mem ? (mem->getLastBus() & 0x3F) : 0x3F;

        const bool exromHigh = cart->getExROMLine();
        const bool gameHigh  = cart->getGameLine();

        uint8_t value = open;
        if (exromHigh) value |= 0x80;
        if (gameHigh)  value |= 0x40;
        return value;
    }
    return 0xFF;
}

void KCSPowerMapper::write(uint16_t address, uint8_t value)
{
    (void)value;

    // IO1: $DE00-$DEFF (mode changes depend on A1 and WRITE)
    if (address >= 0xDE00 && address <= 0xDEFF)
    {
        const bool a1 = (address & 0x02) != 0;

        if (!a1)
        {
            // IO1 WRITE, A1=0 -> 16K GAME
            // EXROM asserted (0/low), GAME asserted (0/low)
            cart->setExROMLine(false);
            cart->setGameLine(false);
        }
        else
        {
            // IO1 WRITE, A1=1 -> ULTIMAX
            // EXROM asserted (0/low), GAME inactive (1/high)
            cart->setExROMLine(false);
            cart->setGameLine(true);
        }
        return;
    }

    // IO2: $DF00-$DFFF
    if (address >= 0xDF00 && address <= 0xDFFF)
    {
        // $DF00-$DF7F = writable cartridge RAM
        if ((address & 0x80) == 0)
        {
            if (cart && cart->hasCartridgeRAM())
                cart->writeRAM(address & 0x7F, value);
            return;
        }

        // $DF80-$DFFF = open area; writes do nothing
        return;
    }
}

bool KCSPowerMapper::loadIntoMemory(uint8_t bank)
{
    bool mapped = false;

    for (const auto& section : cart->getChipSections())
    {
        if (section.data.size() == 8192)
        {
            if (section.loadAddress == 0x8000)
            {
                for (size_t i = 0; i < 8192; ++i)
                    mem->writeCartridge(i, section.data[i], cartLocation::LO);
                mapped = true;
            }
            else if (section.loadAddress == 0xA000)
            {
                for (size_t i = 0; i < 8192; ++i)
                    mem->writeCartridge(i, section.data[i], cartLocation::HI);
                mapped = true;
            }
        }
    }

    return mapped;
}

void KCSPowerMapper::pressFreeze()
{
    // Set Ultimax mode
    cart->setExROMLine(false);
    cart->setGameLine(true);

    // NMI
    if (processor)
        processor->pulseNMI();
}

bool KCSPowerMapper::applyMappingAfterLoad()
{
    // Default: 16K game
    cart->setExROMLine(false); // asserted
    cart->setGameLine(false);  // asserted
    return true;
}
