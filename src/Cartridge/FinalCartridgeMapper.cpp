// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/FinalCartridgeMapper.h"
#include "Memory.h"

FinalCartridgeMapper::FinalCartridgeMapper() :
    processor(nullptr),
    cartEnabled(true)
{

}

FinalCartridgeMapper::~FinalCartridgeMapper() = default;

void FinalCartridgeMapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("FC10");
    wrtr.writeU32(1);            // version
    wrtr.writeBool(cartEnabled); // IO1/IO2 latch
    wrtr.endChunk();
}

bool FinalCartridgeMapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "FC10", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    uint32_t ver = 0;
    if (!rdr.readU32(ver))      { rdr.exitChunkPayload(chunk); return false; }
    if (ver != 1)               { rdr.exitChunkPayload(chunk); return false; }

    bool enabled = true;
    if (!rdr.readBool(enabled)) { rdr.exitChunkPayload(chunk); return false; }

    rdr.exitChunkPayload(chunk);

    cartEnabled = enabled;

    // Re-apply mapping immediately
    applyMappingAfterLoad();

    return true;
}

uint8_t FinalCartridgeMapper::read(uint16_t address)
{
    auto mirror_io_rom = [&](uint16_t addr) -> uint8_t
    {
        // Mirror $9F00-$9FFF from ROML (offset 0x1F00)
        uint16_t offset = 0x1F00 + (addr & 0x00FF);
        return mem->readCartridge(offset, cartLocation::LO);
    };

    if (address >= 0xDE00 && address <= 0xDEFF)
    {
        // Return ROM byte (mirror), THEN disable cart (IO1 => OFF)
        uint8_t value = mirror_io_rom(address);

        if (cartEnabled)
        {
            // OFF: no cart mapped
            cart->setExROMLine(true);
            cart->setGameLine(true);
            cartEnabled = false;
        }
        return value;
    }
    else if (address >= 0xDF00 && address <= 0xDFFF)
    {
        // Return ROM byte (mirror), THEN enable cart (IO2 => ON)
        uint8_t value = mirror_io_rom(address);

        if (!cartEnabled)
        {
            cart->setExROMLine(false);
            cart->setGameLine(false);
            cartEnabled = true;
        }
        return value;
    }

    return 0xFF;
}

void FinalCartridgeMapper::write(uint16_t address, uint8_t value)
{
    if (address >= 0xDE00 && address <= 0xDEFF && cartEnabled)
    {
        // Disable cartridge
        cart->setExROMLine(true);
        cart->setGameLine(true);
        cartEnabled = false;
    }
    else if (address >= 0xDF00 && address <= 0xDFFF && !cartEnabled)
    {
        // Enable cartridge
        cart->setExROMLine(false);
        cart->setGameLine(false);
        cartEnabled = true;
    }
}

bool FinalCartridgeMapper::loadIntoMemory(uint8_t bank)
{
    return true;
}

void FinalCartridgeMapper::pressFreeze()
{
    // Set Ultimax mode
    cart->setExROMLine(false);
    cart->setGameLine(true);

    // NMI
    if (processor)
        processor->pulseNMI();

    // Restore mapping latch state (don't keep forcing Ultimax forever)
    applyMappingAfterLoad();
}

bool FinalCartridgeMapper::applyMappingAfterLoad()
{
    // Re-apply mapping immediately
    if (cartEnabled)
    {
        cart->setExROMLine(false);
        cart->setGameLine(false);
    }
    else
    {
        cart->setExROMLine(true);
        cart->setGameLine(true);
    }

    return true;
}
