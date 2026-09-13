// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/BlackBoxV3Mapper.h"

BlackBoxV3Mapper::BlackBoxV3Mapper() :
    enabled(true)
{

}

BlackBoxV3Mapper::~BlackBoxV3Mapper() = default;

void BlackBoxV3Mapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("BBV3");
    wrtr.writeU32(1);
    wrtr.writeBool(enabled);
    wrtr.endChunk();
}

bool BlackBoxV3Mapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "BBV3", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    uint32_t ver = 0;
    if (!rdr.readU32(ver))          { rdr.exitChunkPayload(chunk); return false; }
    if (ver != 1)                   { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readBool(enabled))     { rdr.exitChunkPayload(chunk); return false; }

    rdr.exitChunkPayload(chunk);
    return true;
}

void BlackBoxV3Mapper::reset()
{
    enabled = true;

    updateLines();
}


uint8_t BlackBoxV3Mapper::read(uint16_t address)
{
    (void)address;

    return cart ? cart->sampleDataBus() : 0xFF;
}

void BlackBoxV3Mapper::write(uint16_t address, uint8_t value)
{
    (void)value;

    if (!cart)
        return;

    // IO1 write disables cartridge ROM.
    if (address >= 0xDE00 && address <= 0xDEFF)
    {
        enabled = false;
        updateLines();
        return;
    }

    // IO2 write enables cartridge ROM.
    if (address >= 0xDF00 && address <= 0xDFFF)
    {
        enabled = true;
        updateLines();
        return;
    }
}

bool BlackBoxV3Mapper::loadIntoMemory(uint8_t bank)
{
    (void)bank;

    if (!cart)
        return false;

    cart->clearCartridge(cartLocation::LO);
    cart->clearCartridge(cartLocation::HI);
    cart->clearCartridge(cartLocation::HI_E000);

    for (const auto& section : cart->getChipSections())
    {
        if (section.loadAddress != 0x8000)
            continue;

        if (section.data.size() < 0x2000)
            continue;

        for (size_t i = 0; i < 0x2000; ++i)
            cart->writeCartridge(static_cast<uint16_t>(i), section.data[i], cartLocation::LO);

        return true;
    }

    return false;
}

bool BlackBoxV3Mapper::applyMappingAfterLoad()
{
    if (!cart)
        return false;

    if (!loadIntoMemory(0))
        return false;

    updateLines();
    return true;
}

void BlackBoxV3Mapper::updateLines()
{
    if (enabled)
    {
        cart->setGameLine(true);
        cart->setExROMLine(false);
    }
    else
    {
        cart->setGameLine(true);
        cart->setExROMLine(true);
    }
}
