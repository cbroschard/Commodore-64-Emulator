// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/Prophet64Mapper.h"

Prophet64Mapper::Prophet64Mapper() :
    selectedBank(0),
    enabled(true)
{

}

Prophet64Mapper::~Prophet64Mapper() = default;

void Prophet64Mapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("P64M");
    wrtr.writeU32(1);

    wrtr.writeU8(selectedBank);
    wrtr.writeBool(enabled);

    wrtr.endChunk();
}

bool Prophet64Mapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "P64M", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    uint32_t ver = 0;
    if (!rdr.readU32(ver))                  { rdr.exitChunkPayload(chunk); return false; }
    if (ver != 1)                           { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readU8(selectedBank))          { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(enabled))             { rdr.exitChunkPayload(chunk); return false; }

    selectedBank &= 0x1F;

    rdr.exitChunkPayload(chunk);
    return true;
}

void Prophet64Mapper::reset()
{
    selectedBank = 0;
    enabled = true;

    loadIntoMemory(selectedBank);
    updateLines();
}

uint8_t Prophet64Mapper::read(uint16_t address)
{
    if (!cart)
        return 0xFF;

    return cart->sampleDataBus();
}

void Prophet64Mapper::write(uint16_t address, uint8_t value)
{
    if (!cart)
        return;

    if (address != 0xDF00)
        return;

    selectedBank = value & 0x1F;

    // Bit 5:
    // 0 = cartridge enabled
    // 1 = cartridge disabled
    enabled = (value & 0x20) == 0;

    if (enabled)
        loadIntoMemory(selectedBank);

    updateLines();
}

bool Prophet64Mapper::loadIntoMemory(uint8_t bank)
{
    if (!cart)
        return false;

    cart->clearCartridge(cartLocation::LO);
    cart->clearCartridge(cartLocation::HI);
    cart->clearCartridge(cartLocation::HI_E000);

    for (const auto& section : cart->getChipSections())
    {
        if (section.bankNumber != bank)
            continue;

        if (section.data.size() != 8192)
            continue;

        for (size_t i = 0; i < 8192; ++i)
            cart->writeCartridge(static_cast<uint16_t>(i), section.data[i], cartLocation::LO);

        return true;
    }

    return false;
}

bool Prophet64Mapper::applyMappingAfterLoad()
{
    if (!cart)
        return false;

    bool mapped = true;

    if (enabled)
        mapped = loadIntoMemory(selectedBank);

    updateLines();

    return mapped;
}

void Prophet64Mapper::updateLines()
{
    if (!cart)
        return;

    if (enabled)
    {
        // Normal 8K cartridge mode:
        // /GAME high, /EXROM low
        cart->setGameLine(true);
        cart->setExROMLine(false);
    }
    else
    {
        // Cartridge disabled:
        // /GAME high, /EXROM high
        cart->setGameLine(true);
        cart->setExROMLine(true);
    }
}
