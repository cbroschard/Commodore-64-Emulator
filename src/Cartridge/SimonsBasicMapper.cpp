// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/SimonsBasicMapper.h"

SimonsBasicMapper::SimonsBasicMapper() :
    highROMEnabled(true)
{

}

SimonsBasicMapper::~SimonsBasicMapper() = default;

void SimonsBasicMapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("SBS0");
    wrtr.writeBool(highROMEnabled);
    wrtr.endChunk();
}

bool SimonsBasicMapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "SBS0", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);
    if (!rdr.readBool(highROMEnabled)) return false;

    return true;
}

bool SimonsBasicMapper::applyMappingAfterLoad()
{
    // Rebuild memory windows according to restored highROMEnabled
    return loadIntoMemory(0);
}

uint8_t SimonsBasicMapper::read(uint16_t address)
{
    if (!cart) return 0xFF;

    if (address == 0xDE00)
    {
        // Return 0x00 on read to set 8k game cartridge
        cart->setExROMLine(false);
        cart->setGameLine(true);
        return 0x00;
    }

    // Open Bus
    return 0xFF;
}

void SimonsBasicMapper::write(uint16_t address, uint8_t value)
{
    if (!cart) return;

    if (address == 0xDE00)
    {
        highROMEnabled = true;
        cart->setGameLine(false);
        cart->setExROMLine(false);
        loadIntoMemory(0);
    }
}

bool SimonsBasicMapper::loadIntoMemory(uint8_t bank)
{
    if (!cart || !mem) return false;

    // Clear LO + HI banks first (fill with 0xFF)
    cart->clearCartridge(cartLocation::LO);
    cart->clearCartridge(cartLocation::HI);

    bool mapped = false;

    // 1) Map low 8K at $8000 unconditionally
    for (const auto &sec : cart->getChipSections()) // Use const ref as we're not modifying sections here
    {
        if (sec.loadAddress == CART_LO_START)
        {
            if (sec.data.size() >= 0x2000) // Ensure it's at least 8K
            {
                for (size_t i = 0; i < 0x2000; ++i) // Map exactly 8K
                    mem->writeCartridge(i, sec.data[i], cartLocation::LO);
                mapped = true;
            }
            else
            {
                std::cerr << "Warning: Simon's Basic low ROM section is less than 8KB!" << std::endl;
                for (size_t i = 0; i < sec.data.size(); ++i)
                    mem->writeCartridge(i, sec.data[i], cartLocation::LO);
                mapped = true;
            }
        }
    }

    // 2) Map high 8K at $A000 only if enabled by bank switching ($DE00)
    if (highROMEnabled) // Use the internal state variable
    {
        for (const auto &sec : cart->getChipSections()) // Use const ref
        {
            if (sec.loadAddress == CART_HI_START) // Assuming CART_HI_START is 0xA000
            {
                if (sec.data.size() >= 0x2000) // Ensure it's at least 8K
                {
                    for (size_t i = 0; i < 0x2000; ++i) // Map exactly 8K
                        mem->writeCartridge(i, sec.data[i], cartLocation::HI);
                    mapped = true;
                }
                else
                {
                    std::cerr << "Warning: Simon's Basic high ROM section is less than 8KB!" << std::endl;
                    for (size_t i = 0; i < sec.data.size(); ++i)
                        mem->writeCartridge(i, sec.data[i], cartLocation::HI);
                    mapped = true;
                }
            }
            else
            {
                // Clear HI region to simulate RAM exposed instead of ROM
                cart->clearCartridge(cartLocation::HI);
            }
        }
    }
    return mapped;
}
