// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/StructuredBasicMapper.h"

StructuredBasicMapper::StructuredBasicMapper() = default;

StructuredBasicMapper::~StructuredBasicMapper() = default;

uint8_t StructuredBasicMapper::read(uint16_t address)
{
    if (address == 0xDE00 || address == 0xDE01)
    {
        loadIntoMemory(0);
    }
    else if (address == 0xDE02)
    {
        loadIntoMemory(1);
    }
    else if (address == 0xDE03)
    {
        cart->setExROMLine(true);
    }

    // Open Bus
    return 0xFF;
}

void StructuredBasicMapper::write(uint16_t address, uint8_t value)
{
    if (address == 0xDE00 || address == 0xDE01)
    {
        loadIntoMemory(0);
    }
    else if (address == 0xDE02)
    {
        loadIntoMemory(1);
    }
    else if (address == 0xDE03)
    {
        cart->setExROMLine(true);
    }
}

bool StructuredBasicMapper::loadIntoMemory(uint8_t bank)
{
    if (!mem || !cart) return false;

    // Clear lo
    cart->clearCartridge(cartLocation::LO);

    const auto& sections = cart->getChipSections();
    for (const auto& sec : sections)
    {
        if (sec.bankNumber == bank && sec.loadAddress == CART_LO_START)
        {
            for (size_t i = 0; i < sec.data.size(); ++i)
                mem->writeCartridge(i, sec.data[i], cartLocation::LO);
        }
    }

    return true;
}

