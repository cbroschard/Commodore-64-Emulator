// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/SuperZaxxonMapper.h"

SuperZaxxonMapper::SuperZaxxonMapper() :
    currentBank(0)
{

}

SuperZaxxonMapper::~SuperZaxxonMapper() = default;

uint8_t SuperZaxxonMapper::read(uint16_t address)
{
    if (address >= 0x8000 && address <= 0x8FFF)
    {
        currentBank = 0;
        loadIntoMemory(currentBank);
        return mem->getCartLOByte(address - 0x8000);
    }
    else if (address >= 0x9000 && address <= 0x9FFF)
    {
        currentBank = 1;
        loadIntoMemory(currentBank);
        return mem->getCartLOByte(address - 0x8000); // mirror, same LO vector
    }
    else if (address >= 0xA000 && address <= 0xBFFF)
    {
        return mem->getCartHIByte(address - 0xA000);
    }

    return 0xFF;
}

void SuperZaxxonMapper::write(uint16_t address, uint8_t value)
{
     // No writable registers on Super Zaxxon
    (void)address;
    (void)value;
}

bool SuperZaxxonMapper::loadIntoMemory(uint8_t bank)
{
    if (!cart || !mem) return false;

    // Clear LO + HI banks first (fill with 0xFF)
    cart->clearCartridge(cartLocation::LO);
    cart->clearCartridge(cartLocation::HI);

    bool mapped = false;

    // Load the fixed 4KB block at $8000 (mirrored at $9000)
    for (const auto& section : cart->getChipSections())
    {
        if (section.loadAddress == CART_LO_START)
        {
            for (size_t i = 0; i < section.data.size(); ++i)
            {
                // Load to $8000 (cartLocation::LO)
                mem->writeCartridge(i, section.data[i], cartLocation::LO);

                // Also mirror to $9000 (if your emulator supports this)
                mem->writeCartridge(i + 0x1000, section.data[i], cartLocation::LO);
            }
            mapped = true;
        }
        else if (section.loadAddress == CART_HI_START && section.bankNumber == 0)
        {
            for (size_t i = 0; i < section.data.size(); ++i)
            {
                mem->writeCartridge(i, section.data[i], cartLocation::HI);
            }
            mapped = true;
        }
    }

    return mapped;
}
