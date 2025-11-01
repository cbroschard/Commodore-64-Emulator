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

    bool loMapped = false;
    bool hiMapped = false;
    bool eMapped  = false;

    // --- LO: banked area ($8000-$9FFF) ---
    cart->clearCartridge(cartLocation::LO);
    for (const auto& section : cart->getChipSections())
    {
        if (section.bankNumber == bank && section.loadAddress == CART_LO_START)
        {
            for (size_t i = 0; i < section.data.size(); ++i)
                mem->writeCartridge(i, section.data[i], cartLocation::LO);
            loMapped = true;
        }
    }

    // --- HI: fixed ($A000-$BFFF), if present ---
    if (cart->hasSectionAt(CART_HI_START))
    {
        cart->clearCartridge(cartLocation::HI);

        uint8_t fixedBank = 0;
        for (const auto& section : cart->getChipSections())
            if (section.loadAddress == CART_HI_START && section.bankNumber > fixedBank)
                fixedBank = section.bankNumber;

        for (const auto& section : cart->getChipSections())
        {
            if (section.bankNumber == fixedBank && section.loadAddress == CART_HI_START)
            {
                for (size_t i = 0; i < section.data.size(); ++i)
                    mem->writeCartridge(i, section.data[i], cartLocation::HI);
                hiMapped = true;
            }
        }
    }

    // --- E000: rarely used, but check if present ---
    if (cart->hasSectionAt(CART_HI_START1))
    {
        cart->clearCartridge(cartLocation::HI);
        for (const auto& section : cart->getChipSections())
        {
            if (section.loadAddress == CART_HI_START1)
            {
                for (size_t i = 0; i < section.data.size(); ++i)
                    mem->writeCartridge(i, section.data[i], cartLocation::HI);
                eMapped = true;
            }
        }
    }

    return loMapped || hiMapped || eMapped;
}
