// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/EasyFlashMapper.h"

EasyFlashMapper::EasyFlashMapper() = default;

EasyFlashMapper::~EasyFlashMapper() = default;

uint8_t EasyFlashMapper::read(uint16_t address)
{
    uint8_t value = 0xFF; // default all bits high

    // Active-low: 0 = asserted, 1 = not asserted
    if (cart->getGameLine())
        value |= (1 << 0);
    else
        value &= ~(1 << 0);

    if (cart->getExROMLine())
        value |= (1 << 1);
    else
        value &= ~(1 << 1);

    // MODE bit (bit 2): EasyFlash = 0
    value &= ~(1 << 2);

    return value;
}

void EasyFlashMapper::write(uint16_t address, uint8_t value)
{
    if (address == 0xDE00)
    {
        loadIntoMemory(value & 0x7F);
    }
    if (address == 0xDE02)
    {
        bool gl = (value & (1 << 0)) != 0;
        bool el = (value & (1 << 1)) != 0;
        cart->setGameLine(!gl);
        cart->setExROMLine(!el);
    }
}

bool EasyFlashMapper::loadIntoMemory(uint8_t bank)
{
    if (!mem || !cart) return false;

    // Clear lo
    cart->clearCartridge(cartLocation::LO);

    const auto& sections = cart->getChipSections();
    for (const auto& sec : sections)
    {
        if (sec.bankNumber == bank && sec.loadAddress == CART_LO_START)
        {
            size_t size = std::min(sec.data.size(), size_t(0x2000));
            for (size_t i = 0; i < size; ++i)
                mem->writeCartridge(i, sec.data[i], cartLocation::LO);
        }
    }
    return true;
}
