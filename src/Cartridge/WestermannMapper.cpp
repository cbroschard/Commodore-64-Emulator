// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/WestermannMapper.h"
#include "Memory.h"

WestermannMapper::WestermannMapper() = default;

WestermannMapper::~WestermannMapper() = default;

void WestermannMapper::saveState(StateWriter& wrtr) const
{
    // no-op
}

bool WestermannMapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    // No-op
    return true;
}

uint8_t WestermannMapper::read(uint16_t address)
{
    if (!cart)
        return 0xFF;

    if (address >= 0xDF00 && address <= 0xDFFF)
    {
        cart->setExROMLine(true);
        cart->setGameLine(true);
    }

    return cart->sampleDataBus();
}

void WestermannMapper::write(uint16_t address, uint8_t value)
{
    // No-op
}

bool WestermannMapper::loadIntoMemory(uint8_t bank)
{
    (void)bank;

    if (!cart || !mem)
        return false;

    cart->clearCartridge(cartLocation::LO);
    cart->clearCartridge(cartLocation::HI);

    bool mapped = false;

    for (const auto& section : cart->getChipSections())
    {
        if (section.data.size() == 0x2000)
        {
            if (section.loadAddress == 0x8000)
            {
                for (size_t i = 0; i < 0x2000; ++i)
                    mem->writeCartridge(static_cast<uint16_t>(i), section.data[i], cartLocation::LO);

                mapped = true;
            }
            else if (section.loadAddress == 0xA000)
            {
                for (size_t i = 0; i < 0x2000; ++i)
                    mem->writeCartridge(static_cast<uint16_t>(i), section.data[i], cartLocation::HI);

                mapped = true;
            }
        }
        else if (section.data.size() == 0x4000 && section.loadAddress == 0x8000)
        {
            for (size_t i = 0; i < 0x2000; ++i)
            {
                mem->writeCartridge(static_cast<uint16_t>(i), section.data[i], cartLocation::LO);
                mem->writeCartridge(static_cast<uint16_t>(i), section.data[0x2000 + i], cartLocation::HI);
            }

            mapped = true;
        }
    }

    return mapped;
}

bool WestermannMapper::applyMappingAfterLoad()
{
     return loadIntoMemory(0);
}

bool WestermannMapper::readDrivesBus(uint16_t address) const
{
    (void)address;
    return false;
}
