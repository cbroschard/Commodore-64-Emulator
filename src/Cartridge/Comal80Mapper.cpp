// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/Comal80Mapper.h"

Comal80Mapper::Comal80Mapper() :
    selectedBank(0)
{

}

Comal80Mapper::~Comal80Mapper() = default;

void Comal80Mapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("CO80");
    wrtr.writeU32(1); // version
    wrtr.writeU8(selectedBank);
    wrtr.endChunk();
}

bool Comal80Mapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "CO80", 4) == 0)
    {
        rdr.enterChunkPayload(chunk);

        uint32_t ver = 0;
        if (!rdr.readU32(ver))          { rdr.exitChunkPayload(chunk); return false; }
        if (ver != 1)                   { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readU8(selectedBank))  { rdr.exitChunkPayload(chunk); return false; }

        // Apply immediately
        if (!applyMappingAfterLoad())   { rdr.exitChunkPayload(chunk); return false; }

        rdr.exitChunkPayload(chunk);
        return true;
    }
    // Not our chunk
    return false;
}

uint8_t Comal80Mapper::read(uint16_t address)
{
    // Open bus
    return 0xFF;
}

void Comal80Mapper::write(uint16_t address, uint8_t value)
{
    if ((address & 0xFF00) != 0xDE00)
        return;

    uint8_t newBank;

    if ((value & 0xFC) == 0x80)         // $80-$83
        newBank = value & 0x03;
    else                                // fallback: 0..3
        newBank = value & 0x03;

    if (newBank != selectedBank)
    {
        selectedBank = newBank;
        applyMappingAfterLoad();
    }
}

bool Comal80Mapper::loadIntoMemory(uint8_t bank)
{
    if (!cart || !mem) return false;

    bank &= 0x03; // COMAL-80 banks 0..3

    // Get sections once (important if getChipSections() returns by value)
    const auto& sections = cart->getChipSections();

    // Find matching 16KB chip for this bank
    size_t matchIndex = static_cast<size_t>(-1);

    for (size_t i = 0; i < sections.size(); ++i)
    {
        const auto& section = sections[i];

        if (section.loadAddress == 0x8000 &&
            section.data.size() == 16384 &&
            section.bankNumber == bank)
        {
            matchIndex = i;
            break;
        }
    }

    if (matchIndex == static_cast<size_t>(-1))
        return false;

    // Now that we KNOW the bank exists, clear and map
    cart->clearCartridge(cartLocation::LO);
    cart->clearCartridge(cartLocation::HI);

    const auto& s = sections[matchIndex];

    for (size_t i = 0; i < 8192; ++i)
        mem->writeCartridge(i, s.data[i], cartLocation::LO);

    for (size_t i = 0; i < 8192; ++i)
        mem->writeCartridge(i, s.data[i + 8192], cartLocation::HI);

    return true;
}

bool Comal80Mapper::applyMappingAfterLoad()
{
    selectedBank &= 0x03;
    return loadIntoMemory(selectedBank);
}
