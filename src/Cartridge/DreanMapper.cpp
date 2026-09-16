// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/DreanMapper.h"

DreanMapper::DreanMapper() :
    selectedBank(0),
    enabled(true)
{

}

DreanMapper::~DreanMapper() = default;

void DreanMapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("DREN");
    wrtr.writeU32(1); // Version

    wrtr.writeU8(selectedBank);
    wrtr.writeBool(enabled);

    wrtr.endChunk();
}

bool DreanMapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "DREN", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    uint32_t ver = 0;
    if (!rdr.readU32(ver))          { rdr.exitChunkPayload(chunk); return false; }
    if (ver != 1)                   { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readU8(selectedBank))  { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(enabled))     { rdr.exitChunkPayload(chunk); return false; }

    rdr.exitChunkPayload(chunk);
    return true;
}

void DreanMapper::reset()
{
    selectedBank = 0;
    enabled = true;

    (void)applyMappingAfterLoad();
}

uint8_t DreanMapper::read(uint16_t address)
{
    if (!cart)
        return 0xFF;

    return cart->sampleDataBus();
}

void DreanMapper::write(uint16_t address, uint8_t value)
{
    if (!cart)
        return;

    // Register mirrored across IO2.
    if (address < 0xDF00 || address > 0xDFFF)
        return;

    selectedBank = value & 0x03;

    // Bit 5 = 1 disables the cartridge.
    enabled = (value & 0x20) == 0;

    if (enabled)
        (void)loadIntoMemory(selectedBank);

    updateLines();
}

bool DreanMapper::loadIntoMemory(uint8_t bank)
{
    if (!cart)
        return false;

    bank &= 0x03;

    cart->clearCartridge(cartLocation::LO);
    cart->clearCartridge(cartLocation::HI);
    cart->clearCartridge(cartLocation::HI_E000);

    for (const auto& section : cart->getChipSections())
    {
        if (section.bankNumber != bank)
            continue;

        if (section.loadAddress != 0x8000)
            continue;

        if (section.data.size() != 8192)
            continue;

        for (size_t i = 0; i < 8192; ++i)
            cart->writeCartridge(static_cast<uint16_t>(i), section.data[i], cartLocation::LO);

        return true;
    }

    return false;
}

bool DreanMapper::applyMappingAfterLoad()
{
    if (!cart)
        return false;

    selectedBank &= 0x03;

    bool mapped = true;

    if (enabled)
        mapped = loadIntoMemory(selectedBank);

    updateLines();

    return mapped;
}

void DreanMapper::updateLines()
{
    if (!cart)
        return;

    if (enabled)
    {
        // 8K cartridge mode
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
