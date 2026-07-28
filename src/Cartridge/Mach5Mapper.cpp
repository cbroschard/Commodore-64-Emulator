// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/Mach5Mapper.h"
#include "Memory.h"

Mach5Mapper::Mach5Mapper() :
    enabled(true),
    loaded(false)
{

}

Mach5Mapper::~Mach5Mapper() = default;

void Mach5Mapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("MACH");
    wrtr.writeU32(1);
    wrtr.writeBool(enabled);
    wrtr.writeBool(loaded);
    wrtr.endChunk();
}

bool Mach5Mapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "MACH", 4) == 0)
    {
        rdr.enterChunkPayload(chunk);

        uint32_t ver = 0;
        if (!rdr.readU32(ver))          { rdr.exitChunkPayload(chunk); return false;}
        if (ver != 1)                   { rdr.exitChunkPayload(chunk); return false;}

        if (!rdr.readBool(enabled))     { rdr.exitChunkPayload(chunk); return false;}

        if (!rdr.readBool(loaded))      { rdr.exitChunkPayload(chunk); return false;}

        // Apply immediately
        if (!applyMappingAfterLoad())   { rdr.exitChunkPayload(chunk); return false;}

        rdr.exitChunkPayload(chunk);
        return true;
    }

    // Not our chunk
    return false;
}

uint8_t Mach5Mapper::read(uint16_t address)
{
    if (!mem)
        return 0xFF;

    if (address >= 0xDE00 && address <= 0xDEFF)
    {
        // $DE00-$DEFF mirrors cartridge ROM $9E00-$9EFF.
        const uint16_t offset =
            static_cast<uint16_t>(0x1E00 + (address & 0x00FF));

        return mem->readCartridge(offset, cartLocation::LO);
    }

    if (address >= 0xDF00 && address <= 0xDFFF)
    {
        // $DF00-$DFFF mirrors cartridge ROM $9F00-$9FFF.
        const uint16_t offset =
            static_cast<uint16_t>(0x1F00 + (address & 0x00FF));

        return mem->readCartridge(offset, cartLocation::LO);
    }

    return 0xFF;
}

void Mach5Mapper::write(uint16_t address, uint8_t value)
{
    (void)value;

    if (!cart)
        return;

    if (address >= 0xDE00 && address <= 0xDEFF)
    {
        // IO1 write: enable 8K cartridge mode.
        enabled = true;
        applyMappingAfterLoad();
        return;
    }

    if (address >= 0xDF00 && address <= 0xDFFF)
    {
        // IO2 write: disable the cartridge.
        enabled = false;
        applyMappingAfterLoad();
    }
}

bool Mach5Mapper::loadIntoMemory(uint8_t bank)
{
    (void)bank;

    if (!mem || !cart)
        return false;

    // MACH 5 is a single 8K ROM mapped at $8000-$9FFF.
    cart->clearCartridge(cartLocation::LO);
    cart->clearCartridge(cartLocation::HI);
    cart->clearCartridge(cartLocation::HI_E000);

    const auto& sections = cart->getChipSections();

    for (const auto& section : sections)
    {
        if (section.loadAddress != 0x8000)
            continue;

        if (section.data.size() < 0x2000)
            continue;

        for (size_t i = 0; i < 0x2000; ++i)
        {
            mem->writeCartridge(
                static_cast<uint16_t>(i),
                section.data[i],
                cartLocation::LO);
        }

        loaded = true;
        return applyMappingAfterLoad();
    }

    loaded = false;
    return false;
}

bool Mach5Mapper::applyMappingAfterLoad()
{
    if (!cart)
        return false;

    // MACH 5 always keeps GAME high.
    // EXROM low enables the 8K ROM; EXROM high disables it.
    cart->setGameLine(true);
    cart->setExROMLine(!enabled);

    return true;
}
