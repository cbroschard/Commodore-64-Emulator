// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/DinamicMapper.h"

DinamicMapper::DinamicMapper() :
    dinamicBank(0)
{
}

DinamicMapper::~DinamicMapper() = default;

uint8_t DinamicMapper::read(uint16_t address)
{
    // ROM reads are already handled by mem, so nothing special
    return 0xFF;
}

void DinamicMapper::write(uint16_t address, uint8_t value)
{
    if (address == 0xDE00)
    {
        dinamicBank = value;
        loadIntoMemory(dinamicBank);
    }
}

bool DinamicMapper::loadIntoMemory(uint8_t bank)
{
    if (!cart || !mem) return false;

    // Clear LO + HI
    cart->clearCartridge(cartLocation::LO);
    cart->clearCartridge(cartLocation::HI);

    bool loMapped = false;
    bool hiMapped = false;

    // --- LO: first 8K of bank ---
    for (const auto& section : cart->getChipSections())
    {
        if (section.bankNumber == bank && section.loadAddress == CART_LO_START)
        {
            for (size_t i = 0; i < section.data.size(); ++i)
                mem->writeCartridge(i, section.data[i], cartLocation::LO);
            loMapped = true;
        }
    }

    // --- HI: second 8K of bank ---
    for (const auto& section : cart->getChipSections())
    {
        if (section.bankNumber == bank && section.loadAddress == CART_HI_START)
        {
            for (size_t i = 0; i < section.data.size(); ++i)
                mem->writeCartridge(i, section.data[i], cartLocation::HI);
            hiMapped = true;
        }
    }

    return loMapped || hiMapped;
}
