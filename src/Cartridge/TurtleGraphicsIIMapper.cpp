// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/TurtleGraphicsIIMapper.h"

TurtleGraphicsIIMapper::TurtleGraphicsIIMapper() :
    selectedBank(0)
{

}

TurtleGraphicsIIMapper::~TurtleGraphicsIIMapper() = default;

void TurtleGraphicsIIMapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("TURT");
    wrtr.writeU32(1); // Version

    wrtr.writeU8(selectedBank);

    wrtr.endChunk();
}

bool TurtleGraphicsIIMapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "TURT", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    uint32_t ver = 0;
    if (!rdr.readU32(ver))          { rdr.exitChunkPayload(chunk); return false; }
    if (ver != 1)                   { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readU8(selectedBank))  { rdr.exitChunkPayload(chunk); return false; }

    selectedBank &= 0x01;

    rdr.exitChunkPayload(chunk);
    return true;
}

void TurtleGraphicsIIMapper::reset()
{
    selectedBank = 0;

    (void)applyMappingAfterLoad();
}

uint8_t TurtleGraphicsIIMapper::read(uint16_t address)
{
    if (!cart)
        return 0xFF;

    if (address >= 0xDE00 && address <= 0xDEFF)
    {
        selectedBank = 1;
        (void)loadIntoMemory(selectedBank);
    }

    return cart->sampleDataBus();
}

void TurtleGraphicsIIMapper::write(uint16_t address, uint8_t value)
{
    (void)value;

    if (!cart)
        return;

    if (address >= 0xDE00 && address <= 0xDEFF)
    {
        selectedBank = 1;
        (void)loadIntoMemory(selectedBank);
    }
}

bool TurtleGraphicsIIMapper::loadIntoMemory(uint8_t bank)
{
    if (!cart)
        return false;

    bank &= 0x01;

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

bool TurtleGraphicsIIMapper::applyMappingAfterLoad()
{
    if (!cart)
        return false;

    selectedBank &= 0x01;

    updateLines();

    return loadIntoMemory(selectedBank);
}

void TurtleGraphicsIIMapper::updateLines()
{
    if (!cart)
        return;

    cart->setGameLine(true);
    cart->setExROMLine(false);
}
