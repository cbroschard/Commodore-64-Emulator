// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/OceanMapper.h"

OceanMapper::OceanMapper() = default;

OceanMapper::~OceanMapper() = default;

uint8_t OceanMapper::read(uint16_t address)
{
    // Default
    return 0xFF;
}

void OceanMapper::write(uint16_t address, uint8_t value)
{
    if (address == 0xDE00)
    {
        cart->setCurrentBank(value & 0x3F);
    }
}

bool OceanMapper::loadIntoMemory(uint8_t bank)
{
    if (!cart || !mem) return false;

    // Clear LO + HI banks first (fill with 0xFF)
    cart->clearCartridge(cartLocation::LO);
    cart->clearCartridge(cartLocation::HI);

    bool loMapped = false;
    bool hiMapped = false;

    // --- LO: banked area ($8000-$9FFF) ---
    for (const auto& section : cart->getChipSections())
    {
        if (section.bankNumber == bank && section.loadAddress == CART_LO_START)
        {
            for (size_t i = 0; i < section.data.size(); ++i)
                mem->writeCartridge(i, section.data[i], cartLocation::LO);
            loMapped = true;
        }
    }

    // --- HI: fixed to highest bank ($A000-$BFFF) ---
    uint8_t maxBank = 0;
    for (const auto& section : cart->getChipSections())
        if (section.loadAddress == CART_HI_START && section.bankNumber > maxBank)
            maxBank = section.bankNumber;

    for (const auto& section : cart->getChipSections())
    {
        if (section.bankNumber == maxBank && section.loadAddress == CART_HI_START)
        {
            for (size_t i = 0; i < section.data.size(); ++i)
                mem->writeCartridge(i, section.data[i], cartLocation::HI);
            hiMapped = true;
        }
    }

    return loMapped || hiMapped; // false if nothing was mapped
}
