// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/StructuredBasicMapper.h"

StructuredBasicMapper::StructuredBasicMapper() :
    selectedBank(0)
{

}

StructuredBasicMapper::~StructuredBasicMapper() = default;

void StructuredBasicMapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("STB0");
    wrtr.writeU8(selectedBank);
    wrtr.endChunk();
}

bool StructuredBasicMapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "STB0", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    if (!rdr.readU8(selectedBank)) return false;
    selectedBank &= 0x01; // only 0/1 valid

    return true;
}

bool StructuredBasicMapper::applyMappingAfterLoad()
{
    // Cartridge already restored EXROM line state in CART chunk.
    // Now rebuild the LO bank mapping:
    return loadIntoMemory(selectedBank);
}

uint8_t StructuredBasicMapper::read(uint16_t address)
{
    if (address == 0xDE00 || address == 0xDE01)
    {
        selectedBank = 0;
        loadIntoMemory(selectedBank);
    }
    else if (address == 0xDE02)
    {
        selectedBank = 1;
        loadIntoMemory(selectedBank);
    }
    else if (address == 0xDE03)
    {
        cart->setExROMLine(true);
    }
    return 0xFF;
}

void StructuredBasicMapper::write(uint16_t address, uint8_t value)
{
    (void)value;

    if (address == 0xDE00 || address == 0xDE01)
    {
        selectedBank = 0;
        loadIntoMemory(selectedBank);
    }
    else if (address == 0xDE02)
    {
        selectedBank = 1;
        loadIntoMemory(selectedBank);
    }
    else if (address == 0xDE03)
    {
        cart->setExROMLine(true);
    }
}

bool StructuredBasicMapper::loadIntoMemory(uint8_t bank)
{
    if (!mem || !cart) return false;

    selectedBank = bank;

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

