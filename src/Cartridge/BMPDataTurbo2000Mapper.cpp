// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/BMPDataTurbo2000Mapper.h"

BMPDataTurbo2000Mapper::BMPDataTurbo2000Mapper() :
    enabled(true)
{

}

BMPDataTurbo2000Mapper::~BMPDataTurbo2000Mapper() = default;

void BMPDataTurbo2000Mapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("BDT2");
    wrtr.writeU32(1);

    wrtr.writeBool(enabled);

    wrtr.endChunk();
}

bool BMPDataTurbo2000Mapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "BDT2", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    uint32_t ver = 0;
    if (!rdr.readU32(ver))      { rdr.exitChunkPayload(chunk); return false; }
    if (ver != 1)              { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readBool(enabled)) { rdr.exitChunkPayload(chunk); return false; }

    updateLines();

    rdr.exitChunkPayload(chunk);
    return true;
}

void BMPDataTurbo2000Mapper::reset()
{
    enabled = true;

    updateLines();
}

uint8_t BMPDataTurbo2000Mapper::read(uint16_t address)
{
    if (!cart)
        return 0xFF;

    return cart->sampleDataBus();
}

void BMPDataTurbo2000Mapper::write(uint16_t address, uint8_t value)
{
    (void)value;

    if (address >= 0xDE00 && address <= 0xDEFF)
    {
        enabled = true;
        updateLines();
    }
    else if (address >= 0xDF00 && address <= 0xDFFF)
    {
        enabled = false;
        updateLines();
    }
}

bool BMPDataTurbo2000Mapper::loadIntoMemory(uint8_t bank)
{
    if (!cart)
        return false;

    cart->clearCartridge(cartLocation::LO);
    cart->clearCartridge(cartLocation::HI);
    cart->clearCartridge(cartLocation::HI_E000);

    for (const auto& section : cart->getChipSections())
    {
        if (section.bankNumber == bank && section.loadAddress == 0x8000 && section.data.size() == 0x4000)
        {
            for (size_t i = 0; i < 0x2000; ++i)
                cart->writeCartridge(static_cast<uint16_t>(i), section.data[i], cartLocation::LO);

            for (size_t i = 0; i < 0x2000; ++i)
                cart->writeCartridge(static_cast<uint16_t>(i), section.data[i + 0x2000], cartLocation::HI);

            updateLines();
            return true;
        }
    }

    return false;
}

bool BMPDataTurbo2000Mapper::applyMappingAfterLoad()
{
    if (!cart)
        return false;

    updateLines();
    return true;
}

void BMPDataTurbo2000Mapper::updateLines()
{
    if (!cart)
        return;

    cart->setGameLine(!enabled);
    cart->setExROMLine(!enabled);
}
