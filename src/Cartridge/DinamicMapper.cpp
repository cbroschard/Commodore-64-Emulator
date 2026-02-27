// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/DinamicMapper.h"

DinamicMapper::DinamicMapper() :
    dinamicBank(0)
{
}

DinamicMapper::~DinamicMapper() = default;

void DinamicMapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("DIN0");
    wrtr.writeU32(1); // version

    wrtr.writeU8(dinamicBank);

    wrtr.endChunk();
}

bool DinamicMapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "DIN0", 4) == 0)
    {
        rdr.enterChunkPayload(chunk);

        uint32_t ver = 0;
        if (!rdr.readU32(ver))          { rdr.exitChunkPayload(chunk); return false; }
        if (ver != 1)                   { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readU8(dinamicBank))   { rdr.exitChunkPayload(chunk); return false; }

        rdr.exitChunkPayload(chunk);
        return true;
    }

    // Not our chunk
    return false;
}

bool DinamicMapper::applyMappingAfterLoad()
{
    return loadIntoMemory(dinamicBank);
}

uint8_t DinamicMapper::read(uint16_t address)
{
    if (address >= 0xDE00 && address <= 0xDEFF)
    {
        uint8_t bank = static_cast<uint8_t>(address & 0x0F);

        dinamicBank = bank;
        loadIntoMemory(dinamicBank);

        return 0xFF;
    }
    return 0xFF;
}

void DinamicMapper::write(uint16_t address, uint8_t value)
{
    (void)address;
    (void)value;
}

bool DinamicMapper::loadIntoMemory(uint8_t bank)
{
    if (!cart || !mem) return false;

    cart->clearCartridge(cartLocation::LO);

    for (const auto& section : cart->getChipSections())
    {
        if (section.bankNumber == bank && section.loadAddress == CART_LO_START)
        {
            for (size_t i = 0; i < section.data.size(); ++i)
                mem->writeCartridge(i, section.data[i], cartLocation::LO);
            return true;
        }
    }
    return false;
}
