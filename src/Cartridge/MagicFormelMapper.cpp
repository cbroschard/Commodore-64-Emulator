// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/MagicFormelMapper.h"

MagicFormelMapper::MagicFormelMapper() :
    romEnabled(true),
    selectedBank(0)
{

}

MagicFormelMapper::~MagicFormelMapper() = default;

void MagicFormelMapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("MGCF");
    wrtr.writeU32(1); // version
    wrtr.writeBool(romEnabled);
    wrtr.writeU8(selectedBank);
    wrtr.endChunk();
}

bool MagicFormelMapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "MGCF", 4) == 0)
    {
        rdr.enterChunkPayload(chunk);
        uint32_t ver = 0;
        if (!rdr.readU32(ver))          { rdr.exitChunkPayload(chunk); return false; }
        if (ver != 1)                   { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(romEnabled))  { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU8(selectedBank))  { rdr.exitChunkPayload(chunk); return false; }

        // Apply immediately
        if (romEnabled)
            loadIntoMemory(selectedBank);
        applyMappingAfterLoad();

        rdr.exitChunkPayload(chunk);
        return true;
    }
    // Not our chunk
    return false;
}

uint8_t MagicFormelMapper::read(uint16_t address)
{
    if (address >= 0xDE00 && address <= 0xDEFF)
    {
        if (romEnabled)
        {
            romEnabled = false;
            applyMappingAfterLoad();
        }
        return 0xFF;

    }
    else if (address >= 0xDF00 && address <= 0xDFFF)
    {
        if (!romEnabled)
        {
            romEnabled = true;
        }
        loadIntoMemory(selectedBank);
        applyMappingAfterLoad();
        return 0xFF;
    }
    return 0xFF;
}

void MagicFormelMapper::write(uint16_t address, uint8_t value)
{
    if (address >= 0xDE00 && address <= 0xDEFF)
    {
        if (romEnabled)
        {
            romEnabled = false;
            applyMappingAfterLoad();
        }
    }
    else if (address >= 0xDF00 && address <= 0xDFFF)
    {
        if (!romEnabled)
        {
            romEnabled = true;
        }
        selectedBank = (value & 0x07);
        loadIntoMemory(selectedBank);
        applyMappingAfterLoad();
    }
}

bool MagicFormelMapper::loadIntoMemory(uint8_t bank)
{
    if (!cart || !mem) return false;

    uint8_t idx = 0;

    for (const auto& section : cart->getChipSections())
    {
        if (section.data.size() != 8192) continue;
        if (section.loadAddress != 0xE000) continue;

        // Select the Nth $E000 8K section as the bank
        if (idx != bank) { idx++; continue; }

        for (size_t i = 0; i < 8192; ++i)
            mem->writeCartridge(static_cast<uint16_t>(i), section.data[i], cartLocation::HI_E000);

        return true;
    }

    return false;
}

bool MagicFormelMapper::applyMappingAfterLoad()
{
    if (!cart) return false;

    if (romEnabled)
    {
        cart->setExROMLine(true);
        cart->setGameLine(false);
        return true;
    }
    else
    {
        // Disable cart
        cart->setExROMLine(true);
        cart->setGameLine(true);
        return true;
    }

    return true;
}
