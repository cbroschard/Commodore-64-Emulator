// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/MultiMAXMapper.h"

MultiMAXMapper::MultiMAXMapper() :
    selectedBank(0),
    loadedBank(0xFF),
    enabled(true)
{

}

MultiMAXMapper::~MultiMAXMapper() = default;

void MultiMAXMapper::reset()
{
    selectedBank = 0;
    loadedBank = 0xFF;
    enabled = true;

    (void)applyMappingAfterLoad();
}

void MultiMAXMapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("MMAX");
    wrtr.writeU32(1); // Version

    wrtr.writeU8(selectedBank);
    wrtr.writeBool(enabled);

    wrtr.endChunk();
}

bool MultiMAXMapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "MMAX", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    uint32_t ver = 0;
    if (!rdr.readU32(ver))              { rdr.exitChunkPayload(chunk); return false; }
    if (ver != 1)                       { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readU8(selectedBank))      { rdr.exitChunkPayload(chunk); return false; }
    if (selectedBank >= 64)             { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readBool(enabled))         { rdr.exitChunkPayload(chunk); return false; }

    rdr.exitChunkPayload(chunk);

    loadedBank = 0xFF;

    updateLines();

    return true;
}

uint8_t MultiMAXMapper::read(uint16_t address)
{
    if (!cart)
        return 0xFF;

    // C64: Read the first 256 bytes of cartridge RAM
    if (address >= 0xDE00 && address <= 0xDEFF)
        return cart->readRAM(address & 0x00FF);

    return cart->sampleDataBus();
}

void MultiMAXMapper::write(uint16_t address, uint8_t value)
{
    (void)value;

    if (!cart)
        return;

    if (address < 0xDE00 || address > 0xDEFF)
        return;

    if (!enabled)
        return;

    selectedBank = static_cast<uint8_t>(address & 0x3F);

    // Bit 7 disables the register until reset.
    enabled = (address & 0x80) == 0;

    (void)applyMappingAfterLoad();
}

bool MultiMAXMapper::loadIntoMemory(uint8_t bank)
{
    for (const auto& section : cart->getChipSections())
    {
        if (section.bankNumber != bank)
            continue;

        if (section.loadAddress != 0x8000)
            continue;

        if (section.data.size() < 0x4000)
            continue;

        // First half -> ROML $8000-$9FFF
        for (size_t i = 0; i < 0x2000; ++i)
            cart->writeCartridge(static_cast<uint16_t>(i), section.data[i], cartLocation::LO);

        // Second half -> ROMH $E000-$FFFF
        for (size_t i = 0; i < 0x2000; ++i)
            cart->writeCartridge(static_cast<uint16_t>(i), section.data[0x2000 + i], cartLocation::HI_E000);

        loadedBank = bank;
        updateLines();
        return true;
    }

    return false;
}

bool MultiMAXMapper::applyMappingAfterLoad()
{
    if (!cart)
        return false;

    if (loadedBank != selectedBank)
    {
        if (!loadIntoMemory(selectedBank))
            return false;
    }

    updateLines();
    return true;
}

void MultiMAXMapper::updateLines()
{
    if (!cart)
        return;

    cart->setGameLine(false);
    cart->setExROMLine(true);
}
