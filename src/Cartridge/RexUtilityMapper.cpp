// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/RexUtilityMapper.h"

RexUtilityMapper::RexUtilityMapper() :
    romEnabled(true)
{

}

RexUtilityMapper::~RexUtilityMapper() = default;

void RexUtilityMapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("REXU");
    wrtr.writeU32(1); // version
    wrtr.writeBool(romEnabled);
    wrtr.endChunk();
}

bool RexUtilityMapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "REXU", 4) == 0)
    {
        rdr.enterChunkPayload(chunk);

        uint32_t ver = 0;
        if (!rdr.readU32(ver))          { rdr.exitChunkPayload(chunk); return false; }
        if (ver != 1)                   { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(romEnabled))  { rdr.exitChunkPayload(chunk); return false; }

        // Apply immediately
        applyMappingAfterLoad();

        rdr.exitChunkPayload(chunk);
        return true;
    }
    // Not our chunk
    return false;
}

uint8_t RexUtilityMapper::read(uint16_t address)
{
    if (address >= 0xDF00 && address <= 0xDFBF)
    {
        if (romEnabled)
        {
            romEnabled = false;
            applyMappingAfterLoad();
        }
    }
    else if (address >= 0xDFC0 && address <= 0xDFFF)
    {
        if (!romEnabled)
        {
            romEnabled = true;
            applyMappingAfterLoad();
        }
    }

    return 0xFF; // Open Bus
}

void RexUtilityMapper::write(uint16_t address, uint8_t value)
{
    // No-op
}

bool RexUtilityMapper::loadIntoMemory(uint8_t bank)
{
    if (!cart || !mem)
        return false;

    bool mapped = false;

    // 8K ROM
    for (const auto& section : cart->getChipSections())
    {
        if (section.data.size() != 8192) continue;

        if (section.loadAddress == 0x8000)   // LO block
        {
            for (size_t i = 0; i < 8192; ++i) mem->writeCartridge(i, section.data[i], cartLocation::LO);
            mapped = true;
        }
    }

    if (mapped)
        applyMappingAfterLoad();

    return mapped;
}

bool RexUtilityMapper::applyMappingAfterLoad()
{
    if (!cart)
        return false;

    if (romEnabled)
    {
        cart->setExROMLine(false);
        cart->setGameLine(true);
    }
    else
    {
        // Cartridge disabled
        cart->setExROMLine(true);
        cart->setGameLine(true);
    }

    return true;
}


