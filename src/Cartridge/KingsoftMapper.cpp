// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/KingsoftMapper.h"

KingsoftMapper::KingsoftMapper() :
    mode(KingsoftMode::Game16K)
{

}

KingsoftMapper::~KingsoftMapper() = default;

void KingsoftMapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("KSFT");
    wrtr.writeU32(1); // Version

    wrtr.writeU8(static_cast<uint8_t>(mode));

    wrtr.endChunk();
}

bool KingsoftMapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "KSFT", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    uint32_t ver = 0;
    if (!rdr.readU32(ver))                                      { rdr.exitChunkPayload(chunk); return false; }
    if (ver != 1)                                               { rdr.exitChunkPayload(chunk); return false; }

    uint8_t tempMode = 0;
    if (!rdr.readU8(tempMode))                                  { rdr.exitChunkPayload(chunk); return false; }
    if (tempMode > static_cast<uint8_t>(KingsoftMode::Ultimax)) { rdr.exitChunkPayload(chunk); return false; }

    mode = static_cast<KingsoftMode>(tempMode);

    rdr.exitChunkPayload(chunk);

    return true;
}

void KingsoftMapper::reset()
{
    mode = KingsoftMode::Game16K;
    (void)applyMappingAfterLoad();
}

uint8_t KingsoftMapper::read(uint16_t address)
{
    if (!cart)
        return 0xFF;

    if (address >= 0xDE00 && address <= 0xDEFF)
    {
        if (mode != KingsoftMode::Game16K)
        {
            mode = KingsoftMode::Game16K;
            (void)applyMappingAfterLoad();
        }
    }

    return cart->sampleDataBus();
}

void KingsoftMapper::write(uint16_t address, uint8_t value)
{
    (void)value;

    if (!cart)
        return;

    if (address >= 0xDE00 && address <= 0xDEFF)
    {
        if (mode != KingsoftMode::Ultimax)
        {
            mode = KingsoftMode::Ultimax;
            (void)applyMappingAfterLoad();
        }
    }
}

bool KingsoftMapper::loadIntoMemory(uint8_t bank)
{
    (void)bank;

    if (!cart)
        return false;

    cart->clearCartridge(cartLocation::LO);
    cart->clearCartridge(cartLocation::HI);
    cart->clearCartridge(cartLocation::HI_E000);

    bool loLoaded = false;
    bool hiLoaded = false;

    for (const auto& section : cart->getChipSections())
    {
        if (section.data.size() != 8192)
            continue;

        if (section.loadAddress != 0x8000)
            continue;

        if (section.bankNumber == 0)
        {
            for (size_t i = 0; i < 8192; ++i)
                cart->writeCartridge(static_cast<uint16_t>(i), section.data[i], cartLocation::LO);

            loLoaded = true;
        }

        if (mode == KingsoftMode::Game16K &&
            section.bankNumber == 1)
        {
            for (size_t i = 0; i < 8192; ++i)
                cart->writeCartridge(static_cast<uint16_t>(i), section.data[i], cartLocation::HI);

            hiLoaded = true;
        }

        if (mode == KingsoftMode::Ultimax &&
            section.bankNumber == 2)
        {
            for (size_t i = 0; i < 8192; ++i)
                cart->writeCartridge(static_cast<uint16_t>(i), section.data[i], cartLocation::HI_E000);

            hiLoaded = true;
        }
    }

    return loLoaded && hiLoaded;
}

bool KingsoftMapper::applyMappingAfterLoad()
{
    if (!cart)
        return false;

    updateLines();

    return loadIntoMemory(0);
}

void KingsoftMapper::updateLines()
{
    if (!cart)
        return;

    switch (mode)
    {
        case KingsoftMode::Game16K:
            cart->setGameLine(false);
            cart->setExROMLine(false);
            break;

        case KingsoftMode::Ultimax:
            cart->setGameLine(false);
            cart->setExROMLine(true);
            break;
    }
}
