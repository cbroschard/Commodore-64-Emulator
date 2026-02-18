// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/RossMapper.h"

RossMapper::RossMapper() :
    selectedBank(0),
    disabled(false)
{

}

RossMapper::~RossMapper() = default;

void RossMapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("ROS0");
    wrtr.writeBool(disabled);
    wrtr.writeU8(selectedBank);
    wrtr.endChunk();
}

bool RossMapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "ROS0", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    if (!rdr.readBool(disabled))   return false;
    if (!rdr.readU8(selectedBank)) return false;

    return true;
}

bool RossMapper::applyMappingAfterLoad()
{
    if (disabled)
    {
        cart->setExROMLine(true);
        cart->setGameLine(true);
        cart->clearCartridge(cartLocation::LO);
        cart->clearCartridge(cartLocation::HI);
        return true;
    }

    // If not disabled, restore mapped state.
    return loadIntoMemory(selectedBank);
}

uint8_t RossMapper::read(uint16_t address)
{
    // Open Bus
    return 0xFF;
}

void RossMapper::write(uint16_t address, uint8_t value)
{
    (void)value;

    if (cart->getCartridgeSize() == 32768 && address == 0xDE00)
    {
        disabled = false;
        selectedBank = 1;
        loadIntoMemory(selectedBank);
    }
    else if (address == 0xDF00)
    {
        disabled = true;

        cart->setExROMLine(true);
        cart->setGameLine(true);
        cart->clearCartridge(cartLocation::LO);
        cart->clearCartridge(cartLocation::HI);
    }
}

bool RossMapper::loadIntoMemory(uint8_t bank)
{
    if (!cart || !mem) return false;

    disabled = false;
    selectedBank = bank;

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
