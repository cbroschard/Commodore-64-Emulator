// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/FunPlayMapper.h"

FunPlayMapper::FunPlayMapper() :
    selectedBank(0)
{

}

FunPlayMapper::~FunPlayMapper() = default;

void FunPlayMapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("FUN0");
    wrtr.writeU8(selectedBank);
    wrtr.endChunk();
}

bool FunPlayMapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "FUN0", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);
    if (!rdr.readU8(selectedBank)) return false;

    return true;
}

uint8_t FunPlayMapper::read(uint16_t address)
{
    // Open Bus
    return 0xFF;
}

void FunPlayMapper::write(uint16_t address, uint8_t value)
{
    if (address != 0xDE00)
        return;

    // decode the bank
    selectedBank = static_cast<uint8_t>(((value & 0x38) >> 3) | ((value & 0x01) << 3));

    // Optional safety (your switch only supports 0..15)
    if (selectedBank > 15)
        return;

    loadIntoMemory(selectedBank);
}

bool FunPlayMapper::loadIntoMemory(uint8_t bank)
{
    if (!cart || !mem) return false;

    // Clear LO
    cart->clearCartridge(cartLocation::LO);

        // Load the selected 8K bank into $8000
    for (const auto& section : cart->getChipSections())
    {
        if (section.bankNumber == bank)
        {
            size_t size = std::min(section.data.size(), static_cast<size_t>(0x2000));
            for (size_t i = 0; i < size; ++i)
                mem->writeCartridge(i, section.data[i], cartLocation::LO);
            return true;
        }
    }
    return false;
}
