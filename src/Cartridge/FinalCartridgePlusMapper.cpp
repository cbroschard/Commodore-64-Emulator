// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/FinalCartridgePlusMapper.h"

FinalCartridgePlusMapper::FinalCartridgePlusMapper() :
    processor(nullptr),
    bit7Latch(0),
    cartDisabled(false),
    rom8000BfffDisabled(false),
    e000Disabled(false)
{

}

FinalCartridgePlusMapper::~FinalCartridgePlusMapper() = default;

void FinalCartridgePlusMapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("FCPL");
    wrtr.writeU32(1); // version
    wrtr.writeU8(bit7Latch);
    wrtr.writeBool(cartDisabled);
    wrtr.writeBool(rom8000BfffDisabled);
    wrtr.writeBool(e000Disabled);
    wrtr.endChunk();
}

bool FinalCartridgePlusMapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "FCPL", 4) == 0)
    {
        rdr.enterChunkPayload(chunk);

        uint32_t ver = 0;
        if (!rdr.readU32(ver))              { rdr.exitChunkPayload(chunk); return false; }
        if (ver != 1)                       { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU8(bit7Latch))         { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(cartDisabled))    { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(rom8000BfffDisabled))    { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(e000Disabled))    { rdr.exitChunkPayload(chunk); return false; }

        // Apply side effects
        if (!applyMappingAfterLoad())           { rdr.exitChunkPayload(chunk); return false; }

        rdr.exitChunkPayload(chunk);
        return true;
    }
    // Not our chunk
    return false;
}

uint8_t FinalCartridgePlusMapper::read(uint16_t address)
{
    // IO2: $DF00-$DFFF
    if (address >= 0xDF00 && address <= 0xDFFF)
    {
        // Bit 7 can be read back *if the cartridge is enabled*.
        if (!cartDisabled)
            return (bit7Latch ? 0x80 : 0x00) | 0x7F;
        else
            return 0xFF;
    }

    return 0xFF;
}

void FinalCartridgePlusMapper::write(uint16_t address, uint8_t value)
{
    if (address >= 0xDF00 && address <= 0xDFFF)
    {
        cartDisabled            = ((value & 0x10) == 0);
        e000Disabled            = ((value & 0x20) == 0);
        rom8000BfffDisabled     = ((value & 0x40) != 0);
        bit7Latch               = (value & 0x80);

        // Take effect immediately
        applyMappingAfterLoad();
    }
}

bool FinalCartridgePlusMapper::loadIntoMemory(uint8_t /*bank*/)
{
    if (!cart || !mem) return false;

    cart->clearCartridge(cartLocation::LO);
    cart->clearCartridge(cartLocation::HI);
    cart->clearCartridge(cartLocation::HI_E000);

    // Try explicit CHIP sections at CPU load addresses ($8000/$A000/$E000)
    bool foundAny = false;

    for (const auto& s : cart->getChipSections())
    {
        if (s.data.size() != 8192) continue;

        if (s.loadAddress == 0x8000)
        {
            for (size_t i = 0; i < 8192; ++i)
                mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::LO);
            foundAny = true;
        }
        else if (s.loadAddress == 0xA000)
        {
            for (size_t i = 0; i < 8192; ++i)
                mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::HI);
            foundAny = true;
        }
        else if (s.loadAddress == 0xE000)
        {
            for (size_t i = 0; i < 8192; ++i)
                mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::HI_E000);
            foundAny = true;
        }
    }

    if (foundAny)
        return true;

    // Fallback: treat as linear 32KiB payload (EPROM layout)
    // FC+ uses only 24KiB: skip first 8KiB, then map:
    // +0x2000 -> $E000, +0x4000 -> $8000, +0x6000 -> $A000
    const auto& sections = cart->getChipSections();

    // Find a single 32KiB section that looks like a linear image
    for (const auto& s : sections)
    {
        if (s.data.size() != 32768) continue;

        if (s.loadAddress != 0x0000) continue;

        const uint8_t* img = s.data.data();

        // $E000-$FFFF <- img[0x2000..0x3FFF]
        for (size_t i = 0; i < 8192; ++i)
            mem->writeCartridge(static_cast<uint16_t>(i), img[0x2000 + i], cartLocation::HI_E000);

        // $8000-$9FFF <- img[0x4000..0x5FFF]
        for (size_t i = 0; i < 8192; ++i)
            mem->writeCartridge(static_cast<uint16_t>(i), img[0x4000 + i], cartLocation::LO);

        // $A000-$BFFF <- img[0x6000..0x7FFF]
        for (size_t i = 0; i < 8192; ++i)
            mem->writeCartridge(static_cast<uint16_t>(i), img[0x6000 + i], cartLocation::HI);

        return true;
    }

    return false;
}

void FinalCartridgePlusMapper::pressFreeze()
{
    // Set Ultimax mode
    cart->setExROMLine(false);
    cart->setGameLine(true);

    // NMI
    if (processor)
        processor->pulseNMI();
}

bool FinalCartridgePlusMapper::isRegionEnabled(CartRegion region) const
{
    if (cartDisabled) return false;

    switch(region)
    {
        case CartRegion::ROM_8000_9FFF:
        case CartRegion::ROM_A000_BFFF:
            return !rom8000BfffDisabled;      // (rename this to rom8000BfffDisabled ideally)
        case CartRegion::ROM_E000_FFFF:
            return !e000Disabled;
        case CartRegion::IO2:
            return true;               // FC+ control is here
        default:
            return true;
    }
}

bool FinalCartridgePlusMapper::applyMappingAfterLoad()
{
    if (!cart)
        return false;

    // When the cart is disabled, it must effectively disappear:
    // both GAME and EXROM inactive (high).
    if (cartDisabled)
    {
        cart->setGameLine(true);   // inactive/high
        cart->setExROMLine(true);  // inactive/high
        return true;
    }

    // Normal FC+ wiring mode: EXROM inactive (high), GAME active (low)
    cart->setExROMLine(true);   // high
    cart->setGameLine(false);   // low

    return true;
}
