// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/C64GameSystemMapper.h"

C64GameSystemMapper::C64GameSystemMapper() = default;

C64GameSystemMapper::~C64GameSystemMapper() = default;

uint8_t C64GameSystemMapper::read(uint16_t address)
{
    if (address >= 0xDE00 && address <= 0xDEFF)
    {
        cart->setExROMLine(true);
        cart->setGameLine(true);
        cart->clearCartridge(cartLocation::LO);
        cart->clearCartridge(cartLocation::HI);
    }

    // Open Bus
    return 0xFF;
}

void C64GameSystemMapper::write(uint16_t address, uint8_t value)
{
    if (address >= 0xDE00 && address <= 0xDEFF)
    {
        loadIntoMemory(address - 0xDE00);
    }
}

bool C64GameSystemMapper::loadIntoMemory(uint8_t bank)
{
    if (!mem || !cart) return false;

    // clear lo and hi
    cart->clearCartridge(cartLocation::LO);
    cart->clearCartridge(cartLocation::HI);

    const auto& sections = cart->getChipSections();
    if (bank >= sections.size())
        return false;

    const auto& sec = sections[bank];
    size_t size = std::min(sec.data.size(), static_cast<size_t>(0x4000)); // 16K
    for (size_t i = 0; i < size; ++i)
    {
        if (i < 0x2000)
            mem->writeCartridge(i, sec.data[i], cartLocation::LO);
        else
            mem->writeCartridge(i - 0x2000, sec.data[i], cartLocation::HI);
    }

    return true;
}
