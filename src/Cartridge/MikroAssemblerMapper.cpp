// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/MikroAssemblerMapper.h"

MikroAssemblerMapper::MikroAssemblerMapper()
{

}

MikroAssemblerMapper::~MikroAssemblerMapper() = default;

void MikroAssemblerMapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("MKRA");
    wrtr.writeU32(1); // version

    wrtr.endChunk();
}

bool MikroAssemblerMapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "MKRA", 4) == 0)
    {
        rdr.enterChunkPayload(chunk);

        uint32_t ver = 0;
        if (!rdr.readU32(ver))          { rdr.exitChunkPayload(chunk); return false; }
        if (ver != 1)                   { rdr.exitChunkPayload(chunk); return false; }

        rdr.exitChunkPayload(chunk);
        return true;
    }

    // Not our chunk
    return false;
}

uint8_t MikroAssemblerMapper::read(uint16_t address)
{
    if (address >= 0xDE00 && address <= 0xDFFF)
    {
        return mem->readCartridge(0x1E00 + (address - 0xDE00), cartLocation::LO);
    }

    // Open Bus
    return 0xFF;
}

void MikroAssemblerMapper::write(uint16_t address, uint8_t value)
{
    // No-op
    (void)address;
    (void)value;
}

bool MikroAssemblerMapper::loadIntoMemory(uint8_t bank)
{
    if (!cart || !mem)
        return false;

    // Mikro Assembler is a single fixed 8K cartridge.
    (void)bank;

    cart->clearCartridge(cartLocation::LO);

    const auto& sections = cart->getChipSections();

    for (const auto& sec : sections)
    {
        // Expect one 8K ROML image at $8000
        if (sec.data.size() == 8192 && sec.loadAddress == 0x8000)
        {
            for (size_t i = 0; i < 8192; ++i)
                mem->writeCartridge(static_cast<uint16_t>(i), sec.data[i], cartLocation::LO);

            return true;
        }
    }

    return false;
}

bool MikroAssemblerMapper::applyMappingAfterLoad()
{
    return loadIntoMemory(0);
}
