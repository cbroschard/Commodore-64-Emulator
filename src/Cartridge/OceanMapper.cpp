// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include <set>
#include <cstring>
#include "Cartridge.h"
#include "Cartridge/OceanMapper.h"

OceanMapper::OceanMapper() :
    builtLists(false),
    sel(0)
{

}

OceanMapper::~OceanMapper() = default;

void OceanMapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("OCN0");
    wrtr.writeU32(1); // version
    wrtr.writeU8(sel);   // 0..63
    wrtr.endChunk();
}

bool OceanMapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "OCN0", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    uint32_t ver = 0;
    if (!rdr.readU32(ver))          { rdr.exitChunkPayload(chunk); return false; }
    if (ver != 1)                   { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readU8(sel))           { rdr.exitChunkPayload(chunk); return false; }
    sel &= 0x3F;

    // Force rebuild of derived lists
    builtLists = false;
    banks.clear();

    rdr.exitChunkPayload(chunk);
    return true;
}

bool OceanMapper::applyMappingAfterLoad()
{
    builtLists = false;
    banks.clear();
    return mapSelectedBank();
}

void OceanMapper::buildBankList()
{
    if (builtLists)
        return;

    std::set<uint16_t> bankSet;

    for (const auto& s : cart->getChipSections())
    {
        if (s.chipType != 0)
            continue;

        if (s.loadAddress == CART_LO_START || s.loadAddress == CART_HI_START)
            bankSet.insert(s.bankNumber);
    }

    banks.assign(bankSet.begin(), bankSet.end());
    builtLists = true;
}

bool OceanMapper::mapSelectedBank()
{
    buildBankList();

    cart->clearCartridge(cartLocation::LO);
    cart->clearCartridge(cartLocation::HI);

    bool wroteLo = false;
    bool wroteHi = false;

    for (const auto& s : cart->getChipSections())
    {
        if (s.chipType != 0)
            continue;

        if (s.bankNumber != sel)
            continue;

        if (s.loadAddress == CART_LO_START)
        {
            if (s.data.size() != 8192)
                continue;

            for (size_t i = 0; i < s.data.size(); ++i)
                mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::LO);

            wroteLo = true;
        }
        else if (s.loadAddress == CART_HI_START)
        {
            if (s.data.size() != 8192)
                continue;

            for (size_t i = 0; i < s.data.size(); ++i)
                mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::HI);

            wroteHi = true;
        }
    }

    return wroteLo || wroteHi;
}

uint8_t OceanMapper::read(uint16_t address)
{
    (void)address;
    return 0xFF;
}

void OceanMapper::write(uint16_t address, uint8_t value)
{
    if (address == 0xDE00)
    {
        cart->setCurrentBank(value & 0x3F);
    }
}

bool OceanMapper::loadIntoMemory(uint8_t bank)
{
    sel = bank & 0x3F;
    return mapSelectedBank();
}
