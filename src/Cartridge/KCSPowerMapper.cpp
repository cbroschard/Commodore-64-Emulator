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

KCSPowerMapper::KCSPowerMapper()
{

}

KCSPowerMapper::~KCSPowerMapper() = default;

void KCSPowerMapper::saveState(StateWriter& wrtr) const
{
    // No-op - cartridge saves RAMData and line states
    (void)wrtr;
}

bool KCSPowerMapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    (void)chunk;
    (void)rdr;
    return false;
}

const char* KCSPowerMapper::getButtonName(uint32_t buttonIndex) const
{
    switch (buttonIndex)
    {
        case 0: return "Freeze";
        case 1: return "Reset";
        default: return "";
    }
}

void KCSPowerMapper::pressButton(uint32_t buttonIndex)
{
    switch (buttonIndex)
    {
        case 0:
            pressFreeze();
            break;

        case 1:
            pressReset();
            break;

        default:
            break;
    }
}

uint8_t KCSPowerMapper::read(uint16_t address)
{
    // IO1: $DE00-$DEFF
    if (address >= 0xDE00 && address <= 0xDEFF)
    {
        const bool a1 = (address & 0x02) != 0;
        if (!a1)
        {
            if (cart)
            {
                // Set 8K mode
                cart->setGameLine(true);
                cart->setExROMLine(false);
            }
        }
        else
        {
            if (cart)
            {
                // "RAM config - Disable cartridge
                cart->setGameLine(true);
                cart->setExROMLine(true);
            }
        }

        uint16_t offset = 0x1E00 + (address & 0x00FF);
        return mem ? mem->readCartridge(offset, cartLocation::LO) : 0xFF;
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

        const bool exromHigh = cart ? cart->getExROMLine() : true;
        const bool gameHigh  = cart ? cart->getGameLine()  : true;

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

    // IO1: $DE00-$DEFF
    if (address >= 0xDE00 && address <= 0xDEFF)
    {
        if (!cart)
            return;

        const bool a1 = (address & 0x02) != 0;

        // A1 sets EXROM, write cycle sets GAME low
        cart->setExROMLine(a1);   // A1=0 -> low/asserted, A1=1 -> high/inactive
        cart->setGameLine(false); // write => GAME low/asserted
        return;
    }

    // IO2: $DF00-$DFFF
    if (address >= 0xDF00 && address <= 0xDFFF)
    {
        if ((address & 0x80) == 0)
        {
            if (cart && cart->hasCartridgeRAM())
                cart->writeRAM(address & 0x7F, value);
            return;
        }

        return;
    }
}

bool KCSPowerMapper::loadIntoMemory(uint8_t bank)
{
    (void)bank;

    if (!cart || !mem)
        return false;

    cart->clearCartridge(cartLocation::LO);
    cart->clearCartridge(cartLocation::HI);

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

bool KCSPowerMapper::applyMappingAfterLoad()
{
    if (!cart || !mem)
        return false;

    if (!loadIntoMemory(0))
        return false;

    cart->setExROMLine(false);
    cart->setGameLine(false);
    return true;
}

void KCSPowerMapper::pressFreeze()
{
    if (!cart) return;

    // Set Ultimax mode
    cart->setExROMLine(false);
    cart->setGameLine(true);

    // NMI
    cart->requestCartridgeNMI();
}

void KCSPowerMapper::pressReset()
{
    if (!cart) return;

    applyMappingAfterLoad();
    cart->requestWarmReset();
}
