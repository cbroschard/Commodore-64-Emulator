// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/ZippCode48Mapper.h"

ZippCode48Mapper::ZippCode48Mapper() :
    enabled(true)
{

}

ZippCode48Mapper::~ZippCode48Mapper() = default;

void ZippCode48Mapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("ZIP4");
    wrtr.writeU32(1);
    wrtr.writeBool(enabled);
    wrtr.endChunk();
}

bool ZippCode48Mapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "ZIP4", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    uint32_t ver = 0;
    if (!rdr.readU32(ver))          { rdr.exitChunkPayload(chunk); return false; }
    if (ver != 1)                   { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readBool(enabled))     { rdr.exitChunkPayload(chunk); return false; }

    rdr.exitChunkPayload(chunk);
    return true;
}

void ZippCode48Mapper::reset()
{
    enabled = true;
    updateLines();
}

uint8_t ZippCode48Mapper::read(uint16_t address)
{
    if (!cart)
        return 0xFF;

    if (address >= 0xDE00 && address <= 0xDEFF)
    {
        enabled = true;
        updateLines();

        const uint16_t offset = static_cast<uint16_t>(0x1E00u + (address & 0x00FF));

        return cart->readCartridge(offset, cartLocation::LO);
    }

    if (address >= 0xDF00 && address <= 0xDFFF)
    {
        enabled = false;
        updateLines();

        return cart->sampleDataBus();
    }

    return cart->sampleDataBus();
}

void ZippCode48Mapper::write(uint16_t address, uint8_t value)
{
    (void)address;
    (void)value;
}

uint8_t ZippCode48Mapper::peek(uint16_t address) const
{
    if (!cart)
        return 0xFF;

    if (address >= 0xDE00 && address <= 0xDEFF)
    {
        const uint16_t offset = static_cast<uint16_t>(0x1E00u + (address & 0x00FF));

        return cart->readCartridge(offset, cartLocation::LO);
    }

    return cart->sampleDataBus();
}

bool ZippCode48Mapper::readDrivesBus(uint16_t address) const
{
    if (!cart)
        return false;

    return address >= 0xDE00 && address <= 0xDEFF;
}

bool ZippCode48Mapper::loadIntoMemory(uint8_t bank)
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

bool ZippCode48Mapper::applyMappingAfterLoad()
{
    if (!cart)
        return false;

    if (!loadIntoMemory(0))
        return false;

    updateLines();
    return true;
}

void ZippCode48Mapper::updateLines()
{
     if (!cart)
        return;

    cart->setGameLine(true);
    cart->setExROMLine(!enabled);
}
