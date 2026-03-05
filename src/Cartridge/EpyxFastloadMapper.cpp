// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/EpyxFastloadMapper.h"

EpyxFastloadMapper::EpyxFastloadMapper() :
    romEnabled(false),
    capacitorCounter(0),
    loaded(false)
{

}

EpyxFastloadMapper::~EpyxFastloadMapper() = default;

void EpyxFastloadMapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("EPYX");
    wrtr.writeU32(1);
    wrtr.writeBool(romEnabled);
    wrtr.writeU16(static_cast<uint16_t>(capacitorCounter));
    wrtr.writeBool(loaded);
    wrtr.endChunk();
}

bool EpyxFastloadMapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "EPYX", 4) == 0)
    {
        rdr.enterChunkPayload(chunk);

        uint32_t ver = 0;
        if (!rdr.readU32(ver))          { rdr.exitChunkPayload(chunk); return false;}
        if (ver != 1)                   { rdr.exitChunkPayload(chunk); return false;}

        if (!rdr.readBool(romEnabled))  { rdr.exitChunkPayload(chunk); return false;}

        uint16_t cc = 0;
        if (!rdr.readU16(cc))           { rdr.exitChunkPayload(chunk); return false;}
        capacitorCounter = cc;

        if (!rdr.readBool(loaded))      { rdr.exitChunkPayload(chunk); return false;}

        // Apply immediately
        if (!applyMappingAfterLoad())   { rdr.exitChunkPayload(chunk); return false;}

        rdr.exitChunkPayload(chunk);
        return true;
    }

    // Not our chunk
    return false;
}

uint8_t EpyxFastloadMapper::read(uint16_t address)
{
    if (!cart || !mem)
        return 0xFF;

    // IO1 read “charges the capacitor”: temporarily enable ROM
    if ((address & 0xFF00) == 0xDE00)
    {
        romEnabled = true;
        capacitorCounter = 512;
        applyMappingAfterLoad();
    }
    return 0xFF;
}

void EpyxFastloadMapper::write(uint16_t address, uint8_t value)
{
    (void)value;

    if (!cart || !mem)
        return;

    if ((address & 0xFF00) == 0xDE00)
    {
        romEnabled = true;
        capacitorCounter = 512;
        applyMappingAfterLoad();
        return;
    }
}

bool EpyxFastloadMapper::loadIntoMemory(uint8_t bank)
{
    (void)bank;

    if (!mem || !cart)
        return false;

    // Epyx FastLoad is an 8K ROM at $8000-$9FFF (ROML).
    cart->clearCartridge(cartLocation::LO);
    cart->clearCartridge(cartLocation::HI);
    cart->clearCartridge(cartLocation::HI_E000);

    const auto& sections = cart->getChipSections();

    for (const auto& s : sections)
    {
        // Most CRTs: one 8K chip at $8000
        if (s.loadAddress != 0x8000)
            continue;

        if (s.data.size() < 0x2000)
            continue;

        for (size_t i = 0; i < 0x2000; ++i)
            mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::LO);

        for (size_t i = 0; i < 0x2000; ++i)
            mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::HI_E000);

        loaded = true;
        return applyMappingAfterLoad();
    }

    loaded = false;
    return false;
}

bool EpyxFastloadMapper::applyMappingAfterLoad()
{
    if (!cart || !mem)
        return false;

    if (romEnabled)
    {
        // 8K cart mapping: EXROM asserted (low), GAME inactive (high)
        cart->setExROMLine(false);
        cart->setGameLine(true);
    }
    else
    {
        // disabled/invisible
        cart->setExROMLine(true);
        cart->setGameLine(true);
    }

    return true;
}

void EpyxFastloadMapper::tick(uint32_t elapsedCycles)
{
    if (!romEnabled || capacitorCounter == 0)
        return;

    if (elapsedCycles >= capacitorCounter)
    {
        capacitorCounter = 0;
        romEnabled = false;
        applyMappingAfterLoad();
    }
    else
    {
        capacitorCounter -= elapsedCycles;
    }
}
