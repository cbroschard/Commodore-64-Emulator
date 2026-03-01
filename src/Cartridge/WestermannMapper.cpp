// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/WestermannMapper.h"

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
    if (address >= 0xDF00 && address <= 0xDFFF)
    {
        // Any read to IO-2 disables the cartridge
        cart->setExROMLine(true);
        cart->setGameLine(true);
    }

    return 0xFF;
}

void WestermannMapper::write(uint16_t address, uint8_t value)
{
    // No-op
}

bool WestermannMapper::loadIntoMemory(uint8_t /*bank*/)
{
    bool mapped = false;

    for (const auto& section : cart->getChipSections())
    {
        // Common case: 8K @ $8000 and 8K @ $A000
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
        // Robust case: 16K @ $8000 (split into LO/HI)
        else if (section.data.size() == 16384 && section.loadAddress == 0x8000)
        {
            for (size_t i = 0; i < 8192; ++i)
                mem->writeCartridge(i, section.data[i], cartLocation::LO);

            for (size_t i = 0; i < 8192; ++i)
                mem->writeCartridge(i, section.data[i + 8192], cartLocation::HI);

            mapped = true;
        }
    }

    return mapped;
}

bool WestermannMapper::applyMappingAfterLoad()
{
     return loadIntoMemory(0);
}
