// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/BISPlusMapper.h"

BISPlusMapper::BISPlusMapper() :
    enabled(true)
{

}

BISPlusMapper::~BISPlusMapper() = default;

void BISPlusMapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("BISP");
    wrtr.writeU32(1); // Version

    wrtr.writeBool(enabled);

    wrtr.endChunk();
}

bool BISPlusMapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "BISP", 4) !=0)
        return false;

    rdr.enterChunkPayload(chunk);

    uint32_t ver = 0;
    if (!rdr.readU32(ver))          { rdr.exitChunkPayload(chunk); return false; }
    if (ver != 1)                   { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readBool(enabled))     { rdr.exitChunkPayload(chunk); return false; }

    rdr.exitChunkPayload(chunk);

    return true;
}

void BISPlusMapper::reset()
{
    enabled = true;
    (void)applyMappingAfterLoad();
}

uint8_t BISPlusMapper::read(uint16_t address)
{
    if (!cart)
        return 0xFF;

    (void)address;

    return cart->sampleDataBus();
}

void BISPlusMapper::write(uint16_t address, uint8_t value)
{
    (void)value;

    if (!cart)
        return;

    if (address != 0xDE00)
        return;

    enabled = false;
    updateLines();
}

bool BISPlusMapper::loadIntoMemory(uint8_t bank)
{
    (void)bank;

    if (!cart)
        return false;

    cart->clearCartridge(cartLocation::LO);
    cart->clearCartridge(cartLocation::HI);
    cart->clearCartridge(cartLocation::HI_E000);

    for (const auto& section : cart->getChipSections())
    {
        if (section.data.empty() || section.data.size() > 8192)
            continue;

        if (section.loadAddress < 0x8000 || section.loadAddress > 0x9FFF)
            continue;

        const uint32_t endAddress = static_cast<uint32_t>(section.loadAddress) + static_cast<uint32_t>(section.data.size());

        if (endAddress > 0xA000)
            continue;

        const uint16_t offset = static_cast<uint16_t>(section.loadAddress - 0x8000);

        for (size_t i = 0; i < section.data.size(); ++i)
            cart->writeCartridge(static_cast<uint16_t>(offset + i), section.data[i], cartLocation::LO);

        return true;
    }

    return false;
}

bool BISPlusMapper::applyMappingAfterLoad()
{
    if (!cart)
        return false;

    const bool mapped = loadIntoMemory(0);

    updateLines();

    return mapped;
}

void BISPlusMapper::updateLines()
{
    if (!cart)
        return;

    if (enabled)
    {
        // Standard 8K cartridge
        cart->setGameLine(true);
        cart->setExROMLine(false);
    }
    else
    {
        // Cartridge disabled
        cart->setGameLine(true);
        cart->setExROMLine(true);
    }
}
