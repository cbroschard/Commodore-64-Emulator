// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/EasyFlashMapper.h"

EasyFlashMapper::EasyFlashMapper() :
    selectedBank(0)
{

}

EasyFlashMapper::~EasyFlashMapper() = default;

void EasyFlashMapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("EF00");
    wrtr.writeU32(1); //version
    wrtr.writeU8(selectedBank);
    wrtr.endChunk();
}

bool EasyFlashMapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "EF00", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    uint32_t ver = 0;
    if (!rdr.readU32(ver))          { rdr.exitChunkPayload(chunk); return false; }
    if (ver != 1)                   { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readU8(selectedBank))  { rdr.exitChunkPayload(chunk); return false; }

    selectedBank &= 0x7F; // safety

    rdr.exitChunkPayload(chunk);
    return true;
}

bool EasyFlashMapper::applyMappingAfterLoad()
{
    // Cartridge has already restored GAME/EXROM in its own loadState.
    // Now rebuild the LO window for the saved bank:
    return loadIntoMemory(selectedBank);
}

uint8_t EasyFlashMapper::read(uint16_t address)
{
    uint8_t value = 0xFF; // default all bits high

    // Active-low: 0 = asserted, 1 = not asserted
    if (cart->getGameLine())
        value |= (1 << 0);
    else
        value &= ~(1 << 0);

    if (cart->getExROMLine())
        value |= (1 << 1);
    else
        value &= ~(1 << 1);

    // MODE bit (bit 2): EasyFlash = 0
    value &= ~(1 << 2);

    return value;
}

void EasyFlashMapper::write(uint16_t address, uint8_t value)
{
    if (address == 0xDE00)
    {
        selectedBank = static_cast<uint8_t>(value & 0x7F);
        loadIntoMemory(value & 0x7F);
    }
    if (address == 0xDE02)
    {
        bool gl = (value & (1 << 0)) != 0;
        bool el = (value & (1 << 1)) != 0;
        cart->setGameLine(!gl);
        cart->setExROMLine(!el);
    }
}

bool EasyFlashMapper::loadIntoMemory(uint8_t bank)
{
    if (!mem || !cart) return false;

    selectedBank = static_cast<uint8_t>(bank & 0x7F);

    cart->clearCartridge(cartLocation::LO);

    for (const auto& sec : cart->getChipSections())
    {
        if (sec.bankNumber == selectedBank && sec.loadAddress == CART_LO_START)
        {
            size_t size = std::min(sec.data.size(), size_t(0x2000));
            for (size_t i = 0; i < size; ++i)
                mem->writeCartridge(i, sec.data[i], cartLocation::LO);
        }
    }
    return true;
}
