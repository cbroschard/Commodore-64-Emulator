// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/SuperGamesMapper.h"

SuperGamesMapper::SuperGamesMapper() :
    writeProtected(false)
{

}

SuperGamesMapper::~SuperGamesMapper() = default;

uint8_t SuperGamesMapper::read(uint16_t address)
{
    return 0xFF;
}

void SuperGamesMapper::write(uint16_t address, uint8_t value)
{
    if (address == 0xDF00 && !writeProtected)
            {
                uint8_t bank = value & 0x03;
                bool disabled = (value >> 2) & 0x01;
                bool writeProtect = (value >> 3) & 0x01;

                cart->setExROMLine(disabled);
                cart->setGameLine(disabled);

                cart->setCurrentBank(bank);

                if (writeProtect) writeProtected = true;
            }
}

bool SuperGamesMapper::loadIntoMemory(uint8_t bank)
{
    if (!cart || !mem) return false;

    // Clear LO + HI banks first (fill with 0xFF)
    cart->clearCartridge(cartLocation::LO);
    cart->clearCartridge(cartLocation::HI);

        if (!cart || !mem)
        return false;

    // Clear both LO and HI areas first (fill with 0xFF)
    cart->clearCartridge(cartLocation::LO);
    cart->clearCartridge(cartLocation::HI);

    const auto& sections = cart->getChipSections();

    bool loaded = false;

    for (const auto& sec : sections)
    {
        if (sec.bankNumber != bank)
            continue;

        if (sec.data.size() == 16384) // 16K section
        {
            // Load LO ($8000–$9FFF)
            for (size_t i = 0; i < 8192; ++i)
                mem->writeCartridge(i, sec.data[i], cartLocation::LO);

            // Load HI ($A000–$BFFF)
            for (size_t i = 8192; i < 16384; ++i)
                mem->writeCartridge(i - 8192, sec.data[i], cartLocation::HI);

            loaded = true;
        }
        else if (sec.data.size() == 8192) // 8K section
        {
            // Use the load address to decide where to map
            if (sec.loadAddress == 0x8000)
            {
                for (size_t i = 0; i < sec.data.size(); ++i)
                    mem->writeCartridge(i, sec.data[i], cartLocation::LO);
            }
            else if (sec.loadAddress == 0xA000)
            {
                for (size_t i = 0; i < sec.data.size(); ++i)
                    mem->writeCartridge(i, sec.data[i], cartLocation::HI);
            }
            loaded = true;
        }
        else
        {
            std::cerr << "SuperGames: Unexpected section size "
                      << sec.data.size() << " in bank " << int(bank) << "\n";
        }
    }

    return loaded;
}
