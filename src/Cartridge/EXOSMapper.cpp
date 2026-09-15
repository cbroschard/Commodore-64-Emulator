// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/EXOSMapper.h"

EXOSMapper::EXOSMapper() = default;

EXOSMapper::~EXOSMapper()
{
    if (cart)
        cart->setExternalKernalActive(false);
}

void EXOSMapper::reset()
{
    (void)applyMappingAfterLoad();
}

void EXOSMapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("EXOS");
    wrtr.writeU32(1); // Version
    wrtr.endChunk();
}

bool EXOSMapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if  (std::memcmp(chunk.tag, "EXOS", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    uint32_t ver = 0;
    if (!rdr.readU32(ver))          { rdr.exitChunkPayload(chunk); return false; }
    if (ver != 1)                   { rdr.exitChunkPayload(chunk); return false; }

    rdr.exitChunkPayload(chunk);
    return true;
}

uint8_t EXOSMapper::read(uint16_t address)
{
    if (!cart)
        return 0xFF;

    (void)address;

    return cart->sampleDataBus();
}

void EXOSMapper::write(uint16_t address, uint8_t value)
{
    (void)address;
    (void)value;
}

bool EXOSMapper::loadIntoMemory(uint8_t bank)
{
    (void)bank;

    if (!cart)
        return false;

    cart->clearCartridge(cartLocation::LO);
    cart->clearCartridge(cartLocation::HI);
    cart->clearCartridge(cartLocation::HI_E000);

    for (const auto& section : cart->getChipSections())
    {
        if (section.data.size() != 8192)
            continue;

        if (section.loadAddress != 0xE000)
            continue;

        for (size_t i = 0; i < 8192; ++i)
            cart->writeCartridge(static_cast<uint16_t>(i), section.data[i], cartLocation::HI_E000);

        return true;
    }

    return false;
}

bool EXOSMapper::applyMappingAfterLoad()
{
    if (!cart)
        return false;

    cart->setExternalKernalActive(true);

    return loadIntoMemory(0);
}
