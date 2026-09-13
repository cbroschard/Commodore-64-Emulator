// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/MAXBASICMapper.h"

MAXBASICMapper::MAXBASICMapper() = default;

MAXBASICMapper::~MAXBASICMapper() = default;

void MAXBASICMapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("MXBM");
    wrtr.writeU32(1);
    wrtr.endChunk();
}

bool MAXBASICMapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "MXBM", 4) == 0)
    {
        rdr.enterChunkPayload(chunk);

        uint32_t ver = 0;
        if (!rdr.readU32(ver))          { rdr.exitChunkPayload(chunk); return false; }
        if (ver !=1 )                   { rdr.exitChunkPayload(chunk); return false; }

        rdr.exitChunkPayload(chunk);
        return true;
    }

    return false;
}

void MAXBASICMapper::reset()
{
    if (!cart)
        return;

    updateLines();
}

uint8_t MAXBASICMapper::read(uint16_t address)
{
    if (!cart)
        return 0xFF;

    if (address >= 0x0800 && address <= 0x0FFF)
        return cart->readRAM(address - 0x0800);

    return cart->sampleDataBus();
}

void MAXBASICMapper::write(uint16_t address, uint8_t value)
{
    if (!cart)
        return;

    if (address >= 0x0800 && address <= 0x0FFF)
        cart->writeRAM(address - 0x0800, value);
}

uint8_t MAXBASICMapper::peek(uint16_t address) const
{
    if (!cart)
        return 0xFF;

    if (address >= 0x0800 && address <= 0x0FFF)
        return cart->peekRAM(address - 0x0800);

    return cart->sampleDataBus();
}

bool MAXBASICMapper::cpuMemoryHandledByMapper(uint16_t address) const
{
    return address >= 0x0800 && address <= 0x0FFF;
}

bool MAXBASICMapper::loadIntoMemory(uint8_t bank)
{
    if (!cart)
        return false;

    (void)bank;

    cart->clearCartridge(cartLocation::LO);
    cart->clearCartridge(cartLocation::HI);
    cart->clearCartridge(cartLocation::HI_E000);

    const auto& sections = cart->getChipSections();

    bool loadedLow = false;
    bool loadedHigh = false;

    for (const auto& s : sections)
    {
        if (s.data.size() < 0x2000)
            continue;

        if (s.loadAddress == 0x8000)
        {
            for (size_t i = 0; i < 0x2000; ++i)
                cart->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::LO);

            loadedLow = true;
        }
        else if (s.loadAddress == 0xE000)
        {
            for (size_t i = 0; i < 0x2000; ++i)
                cart->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::HI_E000);

            loadedHigh = true;
        }
    }

    return loadedLow && loadedHigh;
}

bool MAXBASICMapper::applyMappingAfterLoad()
{
    if (!cart)
        return false;

    if (!loadIntoMemory(0))
        return false;

    updateLines();

    return true;
}

void MAXBASICMapper::updateLines()
{
    if (!cart)
        return;

    cart->setGameLine(false);
    cart->setExROMLine(true);
}
