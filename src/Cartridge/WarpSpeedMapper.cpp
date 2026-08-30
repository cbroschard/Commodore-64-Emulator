// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/WarpSpeedMapper.h"

WarpSpeedMapper::WarpSpeedMapper() :
    enabled(true)
{

}

WarpSpeedMapper::~WarpSpeedMapper() = default;

void WarpSpeedMapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("WARP");
    wrtr.writeU32(1); // version
    wrtr.writeBool(enabled);
    wrtr.endChunk();
}

bool WarpSpeedMapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "WARP", 4) == 0)
    {
        rdr.enterChunkPayload(chunk);

        uint32_t ver = 0;
        if (!rdr.readU32(ver))          { rdr.exitChunkPayload(chunk); return false;}
        if (ver != 1)                   { rdr.exitChunkPayload(chunk); return false;}

        if (!rdr.readBool(enabled))     { rdr.exitChunkPayload(chunk); return false;}

        // Apply immediaetly
        if (!applyMappingAfterLoad())   { rdr.exitChunkPayload(chunk); return false;}

        rdr.exitChunkPayload(chunk);
        return true;
    }
    // Not our chunk
    return false;
}

uint8_t WarpSpeedMapper::read(uint16_t address)
{
    if (!cart)
        return 0xFF;

    if (!enabled)
        return cart->sampleDataBus();

    if (address >= 0xDE00 && address <= 0xDFFF)
    {
        const uint16_t index = static_cast<uint16_t>((address - 0xDE00) & 0x01FF);
        return ioMirror[index];
    }

    return cart->sampleDataBus();
}

void WarpSpeedMapper::write(uint16_t address, uint8_t value)
{
    (void)value;

    if (!cart)
        return;

    if ((address & 0xFF00) == 0xDE00)
    {
        enabled = true;
        (void)applyMappingAfterLoad();
    }
    else if ((address & 0xFF00) == 0xDF00)
    {
        enabled = false;
        (void)applyMappingAfterLoad();
    }
}

bool WarpSpeedMapper::loadIntoMemory(uint8_t /*bank*/)
{
    if (!cart) return false;

    // Clear LO -> HI banks first (fill with 0xFF)
    cart->clearCartridge(cartLocation::LO);
    cart->clearCartridge(cartLocation::HI);

    bool mapped = false;

    for (const auto& section : cart->getChipSections())
    {
        if (section.data.size() == 16384 && section.loadAddress == 0x8000)
        {
            // Lower 8K -> LO ($8000–$9FFF)
            for (size_t i = 0; i < 8192; ++i)
                cart->writeCartridge(i, section.data[i], cartLocation::LO);

            // IO mirror ($DE00-$DFFF) mirrors ROM bytes at $9E00-$9FFF
            // $9E00 - $8000 = 0x1E00, length = 0x200
            for (size_t i = 0; i < 0x200; ++i)
                ioMirror[i] = section.data[0x1E00 + i];

            // Upper 8K -> HI ($A000–$BFFF)
            for (size_t i = 0; i < 8192; ++i)
                cart->writeCartridge(i, section.data[i + 8192], cartLocation::HI);

            mapped = true;
            break; // only one 16K section should exist
        }
    }

    return mapped;
}

bool WarpSpeedMapper::applyMappingAfterLoad()
{
    if (!cart)
        return false;

    if (enabled)
    {
        // Warp Speed is a 16K cartridge.
        cart->setExROMLine(false);
        cart->setGameLine(false);

        return loadIntoMemory(0);
    }

    cart->setExROMLine(true);
    cart->setGameLine(true);

    cart->clearCartridge(cartLocation::LO);
    cart->clearCartridge(cartLocation::HI);

    return true;
}

bool WarpSpeedMapper::readDrivesBus(uint16_t address) const
{
    if (!cart || !enabled)
        return false;

    return address >= 0xDE00 && address <= 0xDFFF;
}
