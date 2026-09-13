// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/BlackBoxV4Mapper.h"

BlackBoxV4Mapper::BlackBoxV4Mapper() :
    enabled(true)
{

}

BlackBoxV4Mapper::~BlackBoxV4Mapper() = default;

void BlackBoxV4Mapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("BBV4");
    wrtr.writeU32(1);
    wrtr.writeBool(enabled);
    wrtr.endChunk();
}

bool BlackBoxV4Mapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "BBV4", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    uint32_t ver = 0;
    if (!rdr.readU32(ver))          { rdr.exitChunkPayload(chunk); return false; }
    if (ver != 1)                   { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readBool(enabled))     { rdr.exitChunkPayload(chunk); return false; }

    rdr.exitChunkPayload(chunk);
    return true;
}

void BlackBoxV4Mapper::reset()
{
    enabled = true;
    updateLines();
}

uint8_t BlackBoxV4Mapper::read(uint16_t address)
{
    // IO1 write disables cartridge ROM.
    if (address >= 0xDE00 && address <= 0xDEFF)
    {
        enabled = false;
        updateLines();
    }

    // IO2 write enables cartridge ROM.
    if (address >= 0xDF00 && address <= 0xDFFF)
    {
        enabled = true;
        updateLines();
    }

    return cart ? cart->sampleDataBus() : 0xFF;
}

void BlackBoxV4Mapper::write(uint16_t address, uint8_t value)
{
    (void)address;
    (void)value;
}

bool BlackBoxV4Mapper::loadIntoMemory(uint8_t bank)
{
    (void)bank;

    if (!cart)
        return false;

    cart->clearCartridge(cartLocation::LO);
    cart->clearCartridge(cartLocation::HI);
    cart->clearCartridge(cartLocation::HI_E000);

    for (const auto& section : cart->getChipSections())
    {
        // Typical BlackBox V4 CRT: one 16K CHIP at $8000.
        if (section.loadAddress == 0x8000 && section.data.size() >= 0x4000)
        {
            // $8000-$9FFF
            for (size_t i = 0; i < 0x2000; ++i)
                cart->writeCartridge(static_cast<uint16_t>(i), section.data[i], cartLocation::LO);

            // $A000-$BFFF
            for (size_t i = 0; i < 0x2000; ++i)
                cart->writeCartridge(static_cast<uint16_t>(i), section.data[0x2000 + i], cartLocation::HI);

            return true;
        }
    }

    return false;
}

bool BlackBoxV4Mapper::applyMappingAfterLoad()
{
    if (!cart)
        return false;

    if (!loadIntoMemory(0))
        return false;

    updateLines();
    return true;
}

void BlackBoxV4Mapper::updateLines()
{
    if (!cart)
        return;

    if (enabled)
    {
        // 16K cartridge mode
        cart->setGameLine(false);
        cart->setExROMLine(false);
    }
    else
    {
        // Cartridge hidden
        cart->setGameLine(true);
        cart->setExROMLine(true);
    }
}
