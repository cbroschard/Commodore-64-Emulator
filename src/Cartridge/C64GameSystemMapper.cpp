// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/C64GameSystemMapper.h"

C64GameSystemMapper::C64GameSystemMapper() :
    selectedBank(0)
{

}

C64GameSystemMapper::~C64GameSystemMapper() = default;

void C64GameSystemMapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("CGS0");
    wrtr.writeU32(1); // version

    wrtr.writeU8(selectedBank);

    wrtr.endChunk();
}

bool C64GameSystemMapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "CGS0", 4) == 0)
    {
        rdr.enterChunkPayload(chunk);

        uint32_t ver = 0;
        if (!rdr.readU32(ver))          { rdr.exitChunkPayload(chunk); return false; }
        if (ver != 1)                   { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readU8(selectedBank))  { rdr.exitChunkPayload(chunk); return false; }

        rdr.exitChunkPayload(chunk);

        return true;
    }

    // Not our chunk
    return false;
}

bool C64GameSystemMapper::applyMappingAfterLoad()
{
    if (!mem || !cart) return false;

    cart->setExROMLine(true);
    cart->setGameLine(true);

    return loadIntoMemory(selectedBank);
}

uint8_t C64GameSystemMapper::read(uint16_t address)
{
    if (address >= 0xDE00 && address <= 0xDEFF)
    {
        cart->setExROMLine(true);
        cart->setGameLine(true);
        cart->clearCartridge(cartLocation::LO);
        cart->clearCartridge(cartLocation::HI);
    }

    // Open Bus
    return 0xFF;
}

void C64GameSystemMapper::write(uint16_t address, uint8_t value)
{
    if (address >= 0xDE00 && address <= 0xDEFF)
    {
        selectedBank = static_cast<uint8_t>(address - 0xDE00);
        loadIntoMemory(selectedBank);
    }
}

bool C64GameSystemMapper::loadIntoMemory(uint8_t bank)
{
    if (!mem || !cart) return false;

    selectedBank = bank;

    cart->clearCartridge(cartLocation::LO);
    cart->clearCartridge(cartLocation::HI);

    const auto& sections = cart->getChipSections();

    // Find chip section whose bankNumber == bank
    const Cartridge::chipSection* sec = nullptr;
    for (const auto& s : sections)
    {
        if (s.bankNumber == bank)
        {
            sec = &s;
            break;
        }
    }
    if (!sec) return false;

    size_t size = std::min(sec->data.size(), static_cast<size_t>(0x4000)); // 16K
    for (size_t i = 0; i < size; ++i)
    {
        if (i < 0x2000) mem->writeCartridge(i, sec->data[i], cartLocation::LO);
        else           mem->writeCartridge(i - 0x2000, sec->data[i], cartLocation::HI);
    }

    return true;
}
