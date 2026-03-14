// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/GenericMapper.h"

GenericMapper::GenericMapper() = default;

GenericMapper::~GenericMapper() = default;

void GenericMapper::saveState(StateWriter& wrtr) const
{
    // No-op
}

bool GenericMapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    // No-op
    return true;
}

uint8_t GenericMapper::read(uint16_t address)
{
    // Return default
    return 0xFF;
}

bool GenericMapper::applyMappingAfterLoad()
{
    // bank doesn’t matter for generic
    return loadIntoMemory(0);
}

void GenericMapper::write(uint16_t address, uint8_t value)
{
    // No-op
}

bool GenericMapper::loadIntoMemory(uint8_t bank)
{
    if (!cart || !mem) return false;

    cart->clearCartridge(cartLocation::LO);
    cart->clearCartridge(cartLocation::HI);
    cart->clearCartridge(cartLocation::HI_E000);

    bool mapped = false;

    // Case 1: single 16K block at $8000 for the selected bank
    for (const auto& section : cart->getChipSections())
    {
        if (section.bankNumber != bank) continue;

        if (section.data.size() == 16384 && section.loadAddress == 0x8000)
        {
            for (size_t i = 0; i < 8192; ++i)
                mem->writeCartridge(static_cast<uint16_t>(i), section.data[i], cartLocation::LO);

            for (size_t i = 0; i < 8192; ++i)
                mem->writeCartridge(static_cast<uint16_t>(i), section.data[i + 8192], cartLocation::HI);

            return true;
        }
    }

    // Case 2: separate 8K blocks for the selected bank
    for (const auto& section : cart->getChipSections())
    {
        if (section.bankNumber != bank) continue;
        if (section.data.size() != 8192) continue;

        if (section.loadAddress == 0x8000)
        {
            for (size_t i = 0; i < 8192; ++i)
                mem->writeCartridge(static_cast<uint16_t>(i), section.data[i], cartLocation::LO);
            mapped = true;
        }
        else if (section.loadAddress == 0xA000)
        {
            for (size_t i = 0; i < 8192; ++i)
                mem->writeCartridge(static_cast<uint16_t>(i), section.data[i], cartLocation::HI);
            mapped = true;
        }
        else if (section.loadAddress == 0xE000)
        {
            for (size_t i = 0; i < 8192; ++i)
                mem->writeCartridge(static_cast<uint16_t>(i), section.data[i], cartLocation::HI_E000);
            mapped = true;
        }
    }

    return mapped;
}
