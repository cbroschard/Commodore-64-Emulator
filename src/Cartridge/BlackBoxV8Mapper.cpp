// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/BlackBoxV8Mapper.h"

BlackBoxV8Mapper::BlackBoxV8Mapper() :
    selectedBank(0),
    gameHigh(false),
    exromHigh(false)
{

}

BlackBoxV8Mapper::~BlackBoxV8Mapper() = default;

void BlackBoxV8Mapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("BBV8");

    wrtr.writeU32(1);

    wrtr.writeU8(selectedBank);
    wrtr.writeBool(gameHigh);

    wrtr.writeBool(exromHigh);

    wrtr.endChunk();
}

bool BlackBoxV8Mapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "BBV8", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    uint32_t ver = 0;
    if (!rdr.readU32(ver))          { rdr.exitChunkPayload(chunk); return false; }
    if (ver != 1)                   { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readU8(selectedBank))  { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readBool(gameHigh))    { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(exromHigh))   { rdr.exitChunkPayload(chunk); return false; }

    rdr.exitChunkPayload(chunk);
    return true;
}

void BlackBoxV8Mapper::reset()
{
    selectedBank = 0;
    gameHigh = false;
    exromHigh = false;

    (void)applyMappingAfterLoad();
}

uint8_t BlackBoxV8Mapper::read(uint16_t address)
{
    (void)address;

    return cart ? cart->sampleDataBus() : 0xFF;
}

void BlackBoxV8Mapper::write(uint16_t address, uint8_t value)
{
    (void)value;

    if (!cart)
        return;

     if (address < 0xDF00 || address > 0xDFFF)
        return;

    const uint8_t reg =
        static_cast<uint8_t>(address & 0x0F);

    exromHigh = (reg & 0x01) != 0;
    gameHigh  = (reg & 0x02) != 0;

    selectedBank = static_cast<uint8_t>((reg >> 2) & 0x03);

    (void)applyMappingAfterLoad();
}

bool BlackBoxV8Mapper::loadIntoMemory(uint8_t bank)
{
    if (!cart)
        return false;

    cart->clearCartridge(cartLocation::LO);
    cart->clearCartridge(cartLocation::HI);
    cart->clearCartridge(cartLocation::HI_E000);

    bool loadedLO = false;
    bool loadedHI = false;

    for (const auto& section : cart->getChipSections())
    {
        if (section.bankNumber != bank)
            continue;

        // Single 16K CHIP at $8000.
        if (section.loadAddress == 0x8000 && section.data.size() >= 0x4000)
        {
            for (size_t i = 0; i < 0x2000; ++i)
            {
                cart->writeCartridge(static_cast<uint16_t>(i), section.data[i], cartLocation::LO);
                cart->writeCartridge(static_cast<uint16_t>(i), section.data[0x2000 + i], cartLocation::HI);
            }

            return true;
        }

        // Split 8K ROML.
        if (section.loadAddress == 0x8000 && section.data.size() >= 0x2000)
        {
            for (size_t i = 0; i < 0x2000; ++i)
                cart->writeCartridge(static_cast<uint16_t>(i), section.data[i], cartLocation::LO);

            loadedLO = true;
            continue;
        }

        // Split 8K ROMH.
        if (section.loadAddress == 0xA000 && section.data.size() >= 0x2000)
        {
            for (size_t i = 0; i < 0x2000; ++i)
                cart->writeCartridge(static_cast<uint16_t>(i), section.data[i], cartLocation::HI);

            loadedHI = true;
        }
    }

    return loadedLO && loadedHI;
}

bool BlackBoxV8Mapper::applyMappingAfterLoad()
{
    if (!cart)
        return false;

    if (!loadIntoMemory(selectedBank))
        return false;

    updateLines();

    return true;
}

void BlackBoxV8Mapper::updateLines()
{
    if (!cart)
        return;

    cart->setGameLine(gameHigh);
    cart->setExROMLine(exromHigh);
}
