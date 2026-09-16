// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/Silverrock128Mapper.h"

Silverrock128Mapper::Silverrock128Mapper() :
    selectedBank(0)
{

}

Silverrock128Mapper::~Silverrock128Mapper() = default;

void Silverrock128Mapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("S128");
    wrtr.writeU32(1); // Version

    wrtr.writeU8(selectedBank);

    wrtr.endChunk();
}

bool Silverrock128Mapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "S128", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    uint32_t ver = 0;
    if (!rdr.readU32(ver))                                          { rdr.exitChunkPayload(chunk); return false; }
    if (ver != 1)                                                   { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readU8(selectedBank))                                  { rdr.exitChunkPayload(chunk); return false; }

    rdr.exitChunkPayload(chunk);
    return true;

}

void Silverrock128Mapper::reset()
{
    selectedBank = 0;

    (void)applyMappingAfterLoad();
}

uint8_t Silverrock128Mapper::read(uint16_t address)
{
    (void)address;

    if (!cart)
        return 0xFF;

    return cart->sampleDataBus();
}

void Silverrock128Mapper::write(uint16_t address, uint8_t value)
{
    if (!cart)
        return;

    if (address < 0xDE00 || address > 0xDEFF)
        return;

    static constexpr uint8_t bankSeq[16] =
    {
        0, 2, 4, 6, 8, 10, 12, 14,
        1, 3, 5, 7, 9, 11, 13, 15
    };

    const uint8_t offset =
        static_cast<uint8_t>(address & 0x00FF);

    uint8_t bankNumber = 0;

    if (offset == 0x00)
    {
        const uint8_t bankIndex =
            static_cast<uint8_t>((value & 0xF0) >> 4);

        bankNumber = bankSeq[bankIndex];
    }
    else if (offset <= 0x0F)
    {
        bankNumber = offset;

        const uint8_t bankIndex = static_cast<uint8_t>((value & 0xF0) >> 4);

        if (bankNumber != bankSeq[bankIndex])
            bankNumber = 0;
    }
    else
        bankNumber = 0;

    selectedBank = bankNumber;

    (void)loadIntoMemory(selectedBank);
}

bool Silverrock128Mapper::loadIntoMemory(uint8_t bank)
{
    if (!cart)
        return false;

    bank &= 0x0F;

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

bool Silverrock128Mapper::applyMappingAfterLoad()
{
    if (!cart)
        return false;

    selectedBank &= 0x0F;

    updateLines();

    return loadIntoMemory(selectedBank);
}

void Silverrock128Mapper::updateLines()
{
    if (!cart)
        return;

    cart->setGameLine(true);
    cart->setExROMLine(false);
}
