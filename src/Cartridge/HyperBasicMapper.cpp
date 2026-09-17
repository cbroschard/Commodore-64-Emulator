// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/HyperBasicMapper.h"

HyperBasicMapper::HyperBasicMapper() :
    selectedBank(0),
    enabled(true)
{

}

HyperBasicMapper::~HyperBasicMapper() = default;

void HyperBasicMapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("HYPR");
    wrtr.writeU32(1); // Version

    wrtr.writeU8(selectedBank);
    wrtr.writeBool(enabled);

    wrtr.endChunk();
}

bool HyperBasicMapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "HYPR", 4) != 0)
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

void HyperBasicMapper::reset()
{
    selectedBank = 0;
    enabled = true;

    (void)applyMappingAfterLoad();
}

uint8_t HyperBasicMapper::read(uint16_t address)
{
    (void)address;

    if (!cart)
        return 0xFF;

    return cart->sampleDataBus();
}

void HyperBasicMapper::write(uint16_t address, uint8_t value)
{
    if (!cart)
        return;

    if (address < 0xDE00 || address > 0xDEFF)
        return;

    const bool rom1Enabled = (value & 0x02) == 0;
    const bool rom2Enabled = (value & 0x01) == 0;

    const uint8_t subBank = static_cast<uint8_t>((value >> 4) & 0x03);

    if (rom1Enabled && !rom2Enabled)
        selectedBank = subBank;       // 0-3
    else if (rom2Enabled && !rom1Enabled)
        selectedBank = 4 + subBank;   // 4-7
    // Invalid/undefined chip-select combination: keep previous bank

    enabled = (value & 0xC0) != 0xC0;

    (void)applyMappingAfterLoad();
}

bool HyperBasicMapper::loadIntoMemory(uint8_t bank)
{
    if (!cart)
        return false;

    bank &= 0x07;

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

bool HyperBasicMapper::applyMappingAfterLoad()
{
    if (!cart)
        return false;

    selectedBank &= 0x07;

    updateLines();

    return loadIntoMemory(selectedBank);
}

void HyperBasicMapper::updateLines()
{
    if (!cart)
        return;

    cart->setGameLine(true);
    cart->setExROMLine(!enabled);
}
