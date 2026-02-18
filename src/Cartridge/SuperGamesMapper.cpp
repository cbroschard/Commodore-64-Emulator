// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/SuperGamesMapper.h"

SuperGamesMapper::SuperGamesMapper() :
    selectedBank(0),
    disabled(false),
    writeProtected(false)
{

}

SuperGamesMapper::~SuperGamesMapper() = default;

void SuperGamesMapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("SGM0");
    wrtr.writeU8(selectedBank);
    wrtr.writeBool(disabled);
    wrtr.writeBool(writeProtected);
    wrtr.endChunk();
}

bool SuperGamesMapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "SGM0", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    if (!rdr.readU8(selectedBank)) return false;
    if (!rdr.readBool(disabled)) return false;
    if (!rdr.readBool(writeProtected)) return false;

    selectedBank &= 0x03; // safety
    return true;
}

bool SuperGamesMapper::applyMappingAfterLoad()
{
    cart->setExROMLine(disabled);
    cart->setGameLine(disabled);

    if (disabled)
    {
        cart->clearCartridge(cartLocation::LO);
        cart->clearCartridge(cartLocation::HI);
        return true;
    }

    return loadIntoMemory(selectedBank);
}

uint8_t SuperGamesMapper::read(uint16_t address)
{
    return 0xFF;
}

void SuperGamesMapper::write(uint16_t address, uint8_t value)
{
    if (address != 0xDF00 || writeProtected)
        return;

    selectedBank = value & 0x03;
    disabled     = ((value >> 2) & 0x01) != 0;
    bool wp      = ((value >> 3) & 0x01) != 0;

    cart->setExROMLine(disabled);
    cart->setGameLine(disabled);

    cart->setCurrentBank(selectedBank);

    if (wp) writeProtected = true;
}

bool SuperGamesMapper::loadIntoMemory(uint8_t bank)
{
    if (!cart || !mem) return false;

    bank &= 0x03;
    selectedBank = bank;
    disabled = false; // mapping implies enabled

    cart->clearCartridge(cartLocation::LO);
    cart->clearCartridge(cartLocation::HI);

    const auto& sections = cart->getChipSections();
    bool loaded = false;

    for (const auto& sec : sections)
    {
        if (sec.bankNumber != bank) continue;

        if (sec.data.size() == 16384)
        {
            for (size_t i = 0; i < 8192; ++i)
                mem->writeCartridge(i, sec.data[i], cartLocation::LO);
            for (size_t i = 8192; i < 16384; ++i)
                mem->writeCartridge(i - 8192, sec.data[i], cartLocation::HI);
            loaded = true;
        }
        else if (sec.data.size() == 8192)
        {
            if (sec.loadAddress == 0x8000)
                for (size_t i = 0; i < 8192; ++i)
                    mem->writeCartridge(i, sec.data[i], cartLocation::LO);
            else if (sec.loadAddress == 0xA000)
                for (size_t i = 0; i < 8192; ++i)
                    mem->writeCartridge(i, sec.data[i], cartLocation::HI);

            loaded = true;
        }
    }
    return loaded;
}
