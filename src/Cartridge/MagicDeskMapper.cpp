// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/MagicDeskMapper.h"

MagicDeskMapper::MagicDeskMapper() :
    magicDeskBank(0)
{

}

MagicDeskMapper::~MagicDeskMapper() = default;

uint8_t MagicDeskMapper::read(uint16_t address)
{
    if (address == 0xDE00)
    {
        return magicDeskBank;
    }

    // Default open Bus
    return 0xFF;
}

void MagicDeskMapper::write(uint16_t address, uint8_t value)
{
    if (address == 0xDE00)
    {
        uint8_t newBank = (value & 0x7F);

        bool disable = (value & 0x80) != 0;
        if(disable)
        {
            cart->setGameLine(true);
            cart->setExROMLine(true);
            cart->clearCartridge(cartLocation::LO);
        }
        else
        {
            cart->setGameLine(true);
            cart->setExROMLine(false);
        }

        if (magicDeskBank != newBank)
        {
            magicDeskBank = newBank;
            cart->setCurrentBank(magicDeskBank);
        }
    }
}

bool MagicDeskMapper::loadIntoMemory(uint8_t bank) {
    if (!cart || !mem) return false;

    cart->clearCartridge(cartLocation::LO);

    const auto& sections = cart->getChipSections();
    for (const auto& sec : sections) {
        if (sec.bankNumber == bank && sec.loadAddress == CART_LO_START) {
            size_t size = std::min(sec.data.size(), size_t(0x2000));
            for (size_t i = 0; i < size; ++i)
                mem->writeCartridge(i, sec.data[i], cartLocation::LO);
            return true;
        }
    }

    std::cerr << "MagicDesk: Bank " << unsigned(bank) << " not found.\n";
    return false;
}
