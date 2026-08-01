// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/SuperZaxxonMapper.h"
#include "Memory.h"

SuperZaxxonMapper::SuperZaxxonMapper() :
    currentBank(0)
{

}

SuperZaxxonMapper::~SuperZaxxonMapper() = default;

void SuperZaxxonMapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("SZX0");
    wrtr.writeU32(1); // version
    wrtr.writeU8(currentBank);
    wrtr.endChunk();
}

bool SuperZaxxonMapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "SZX0", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    uint32_t ver = 0;
    if (!rdr.readU32(ver))          { rdr.exitChunkPayload(chunk); return false; }
    if (ver != 1)                   { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readU8(currentBank))   { rdr.exitChunkPayload(chunk); return false; }

    rdr.exitChunkPayload(chunk);
    return true;
}

bool SuperZaxxonMapper::applyMappingAfterLoad()
{
    return loadIntoMemory(currentBank);
}

uint8_t SuperZaxxonMapper::read(uint16_t address)
{
    if (!cart)
        return 0xFF;

    if (address >= 0x8000 && address <= 0x9FFF)
    {
        if (address <= 0x8FFF)
            currentBank = 0;
        else
            currentBank = 1;

        const size_t offset =
            static_cast<size_t>((address - 0x8000) & 0x0FFF);

        for (const auto& section : cart->getChipSections())
        {
            if (section.loadAddress != 0x8000)
                continue;

            if (section.bankNumber != 0)
                continue;

            if (offset >= section.data.size())
                return 0xFF;

            return section.data[offset];
        }

        return 0xFF;
    }

    /*
     * ROMH is selected by the most recent read from either ROML mirror.
     */
    if (address >= 0xA000 && address <= 0xBFFF)
    {
        const size_t offset =
            static_cast<size_t>(address - 0xA000);

        for (const auto& section : cart->getChipSections())
        {
            if (section.loadAddress != 0xA000)
                continue;

            if (section.bankNumber != currentBank)
                continue;

            if (offset >= section.data.size())
                return 0xFF;

            return section.data[offset];
        }

        return 0xFF;
    }

    return 0xFF;
}

void SuperZaxxonMapper::write(uint16_t address, uint8_t value)
{
     // No writable registers on Super Zaxxon
    (void)address;
    (void)value;
}

bool SuperZaxxonMapper::loadIntoMemory(uint8_t bank)
{
    if (!cart || !mem) return false;

    // Clear LO + HI banks first (fill with 0xFF)
    cart->clearCartridge(cartLocation::LO);
    cart->clearCartridge(cartLocation::HI);

    bool mapped = false;

    // Load the fixed 4KB block at $8000 (mirrored at $9000)
    for (const auto& section : cart->getChipSections())
    {
        if (section.loadAddress == CART_LO_START)
        {
            for (size_t i = 0; i < section.data.size(); ++i)
            {
                // Load to $8000 (cartLocation::LO)
                mem->writeCartridge(i, section.data[i], cartLocation::LO);
                mem->writeCartridge(i + 0x1000, section.data[i], cartLocation::LO);
            }
            mapped = true;
        }
        else if (section.loadAddress == CART_HI_START && section.bankNumber == 0)
        {
            for (size_t i = 0; i < section.data.size(); ++i)
                mem->writeCartridge(i, section.data[i], cartLocation::HI);

            mapped = true;
        }
    }

    return mapped;
}

bool SuperZaxxonMapper::romReadHandledByMapper(uint16_t address) const
{
    return address >= 0x8000 && address <= 0xBFFF;
}
