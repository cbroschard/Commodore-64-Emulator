// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
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
    wrtr.writeU8(sel);   // 0..63
    wrtr.endChunk();
}

bool OceanMapper::applyMappingAfterLoad()
{
    // ensure lists exist, then map current selection
    builtLists = false;          // buildBankLists() will do nothing if already built
    return mapPair(static_cast<size_t>(sel));
}

bool OceanMapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "OCN0", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    if (!rdr.readU8(sel)) return false;
    sel &= 0x3F;

    // Force rebuild of derived lists
    builtLists = false;
    loBanks.clear();
    hiBanks.clear();

    return true;
}

void OceanMapper::buildBankLists()
{
    if (builtLists) return;

    std::set<uint16_t> loSet, hiSet;
    for (const auto& s : cart->getChipSections())
    {
        if (s.chipType != 0) continue;
        if (s.loadAddress == CART_LO_START) loSet.insert(s.bankNumber);
        else if (s.loadAddress == CART_HI_START || s.loadAddress == CART_HI_START1) hiSet.insert(s.bankNumber);
    }
    loBanks.assign(loSet.begin(), loSet.end());
    hiBanks.assign(hiSet.begin(), hiSet.end());
    builtLists = true;
}

bool OceanMapper::mapPair(size_t index)
{
    buildBankLists();

    cart->clearCartridge(cartLocation::LO);
    const bool hasHI = !hiBanks.empty();
    if (hasHI) cart->clearCartridge(cartLocation::HI);

    bool wroteLo = false, wroteHi = false;

    if (!loBanks.empty()) {
        const uint16_t bankLo = loBanks[ index % loBanks.size() ];
        for (const auto& s : cart->getChipSections())
        {
            if (s.bankNumber == bankLo && s.loadAddress == CART_LO_START)
            {
                if (s.data.size() == 16384)
                {
                    // split 16K-at-$8000 if it ever appears
                    for (size_t i = 0; i < 8192; ++i)
                        mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::LO);
                    wroteLo = true;
                    if (hasHI)
                    {
                        for (size_t i = 0; i < 8192; ++i)
                            mem->writeCartridge(static_cast<uint16_t>(i), s.data[8192 + i], cartLocation::HI);
                        wroteHi = true;
                    }
                }
                else
                {
                    for (size_t i = 0; i < s.data.size(); ++i)
                        mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::LO);
                    wroteLo = true;
                }
            }
        }
    }

    if (hasHI) {
        const uint16_t bankHi = hiBanks[ index % hiBanks.size() ];
        for (const auto& s : cart->getChipSections())
        {
            if (s.bankNumber == bankHi && (s.loadAddress == CART_HI_START || s.loadAddress == CART_HI_START1))
            {
                for (size_t i = 0; i < s.data.size(); ++i)
                    mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::HI);
                wroteHi = true;
            }
        }
    }

    return wroteLo || wroteHi;
}

uint8_t OceanMapper::read(uint16_t address)
{
    // Default
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
    return mapPair(static_cast<size_t>(sel));
}
