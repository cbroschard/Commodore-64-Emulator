// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/FreezeFrameMK2Mapper.h"

FreezeFrameMK2Mapper::FreezeFrameMK2Mapper() :
    selectedBank(0)
{

}

FreezeFrameMK2Mapper::~FreezeFrameMK2Mapper() = default;

void FreezeFrameMK2Mapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("FMK2");
    wrtr.writeU32(1); // version

    wrtr.writeU8(selectedBank);

    wrtr.endChunk();
}

bool FreezeFrameMK2Mapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "FMK2", 4) == 0)
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

uint8_t FreezeFrameMK2Mapper::read(uint16_t address)
{
    if (address >= 0xDE00 && address <= 0xDEFF)
        setBank(1);

    return 0xFF;
}

void FreezeFrameMK2Mapper::write(uint16_t address, uint8_t value)
{
    (void)value;

    if (address >= 0xDE00 && address <= 0xDEFF)
        setBank(1);
}

void FreezeFrameMK2Mapper::reset()
{
    setBank(0);
}

bool FreezeFrameMK2Mapper::loadIntoMemory(uint8_t bank)
{
    if (!cart || !mem)
        return false;

    bank &= 0x01; // MK2: bank 0 or 1

    cart->clearCartridge(cartLocation::LO);
    cart->clearCartridge(cartLocation::HI);
    cart->clearCartridge(cartLocation::HI_E000);

    const auto& sections = cart->getChipSections();

    // Preferred case: CRT stores two separate 8K banks
    for (const auto& section : sections)
    {
        if (section.bankNumber != bank)
            continue;

        if (section.data.size() != 8192)
            continue;

        // MK2 visible bank is ROML only
        for (size_t i = 0; i < 8192; ++i)
            mem->writeCartridge(static_cast<uint16_t>(i), section.data[i], cartLocation::LO);

        return true;
    }

    // Fallback: tolerate a single 16K image and split it into 2 x 8K banks
    for (const auto& section : sections)
    {
        if (section.data.size() != 16384)
            continue;

        const size_t base = (bank == 0) ? 0 : 8192;
        for (size_t i = 0; i < 8192; ++i)
            mem->writeCartridge(static_cast<uint16_t>(i), section.data[base + i], cartLocation::LO);

        return true;
    }

    return false;
}

const char* FreezeFrameMK2Mapper::getButtonName(uint32_t buttonIndex) const
{
    switch(buttonIndex)
    {
        case 0:
            return "Freeze";
        default:
            return "";
    }
}

void FreezeFrameMK2Mapper::pressButton(uint32_t buttonIndex)
{
    switch(buttonIndex)
    {
        case 0:
            pressFreeze();
            break;
        default:
            break;
    }
}

void FreezeFrameMK2Mapper::pressFreeze()
{
    if (cart)
        cart->requestCartridgeNMI();
}

bool FreezeFrameMK2Mapper::applyMappingAfterLoad()
{
    return setBank(selectedBank);
}

bool FreezeFrameMK2Mapper::setBank(uint8_t bank)
{
    selectedBank = bank & 0x01;
    return loadIntoMemory(selectedBank);
}
