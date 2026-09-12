// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/FreezeMachineMapper.h"

FreezeMachineMapper::FreezeMachineMapper() :
    mode(Mode::Normal),
    normal16K(false),
    selectedHalf(0)
{

}

FreezeMachineMapper::~FreezeMachineMapper() = default;

void FreezeMachineMapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("FRZM");
    wrtr.writeU32(1); // version

    wrtr.writeU8(static_cast<uint8_t>(mode));
    wrtr.writeBool(normal16K);
    wrtr.writeU8(selectedHalf);

    wrtr.endChunk();
}

bool FreezeMachineMapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "FRZM", 4) == 0)
    {
        rdr.enterChunkPayload(chunk);

        uint32_t ver = 0;
        if (!rdr.readU32(ver))          { rdr.exitChunkPayload(chunk); return false; }
        if (ver != 1)                   { rdr.exitChunkPayload(chunk); return false; }

        uint8_t modeU8 = 0;
        if (!rdr.readU8(modeU8))        { rdr.exitChunkPayload(chunk); return false; }
        mode = static_cast<Mode>(modeU8);

        if (!rdr.readBool(normal16K))   { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU8(selectedHalf))  { rdr.exitChunkPayload(chunk); return false; }

        if (!applyMappingAfterLoad())   { rdr.exitChunkPayload(chunk); return false; }

        rdr.exitChunkPayload(chunk);
        return true;
    }

    // Not our chunk
    return false;
}

void FreezeMachineMapper::reset()
{
    if (!cart)
        return;

    const auto& sections = cart->getChipSections();

    bool hasUpperHalf = false;

    for (const auto& sec : sections)
    {
        if (sec.bankNumber >= 2)
        {
            hasUpperHalf = true;
            break;
        }
    }

    if (hasUpperHalf)
        selectedHalf ^= 1;
    else
        selectedHalf = 0;

    mode = Mode::Normal;
    normal16K = false;

    cart->setGameLine(true);
    cart->setExROMLine(false);

    loadIntoMemory(selectedHalf);
}

uint8_t FreezeMachineMapper::read(uint16_t address)
{

    if (address >= 0xDE00 && address <= 0xDEFF)
    {
         // IO1 read -> 16K game mode
        mode = Mode::Normal;
        normal16K = true;

        cart->setGameLine(false);
        cart->setExROMLine(false);

        loadIntoMemory(selectedHalf);
    }
    else if (address >= 0xDF00 && address <= 0xDFFF)
    {
         // IO2 read -> cartridge disabled
        mode = Mode::Disabled;
        normal16K = false;

        cart->setGameLine(true);
        cart->setExROMLine(true);
    }

    return cart ? cart->sampleDataBus() : 0xFF;
}

void FreezeMachineMapper::write(uint16_t address, uint8_t value)
{
    // No-op
    (void)address;
    (void)value;
}

bool FreezeMachineMapper::loadIntoMemory(uint8_t bank)
{
    if (!cart)
        return false;

    if (mode == Mode::Disabled)
        return true;

    cart->clearCartridge(cartLocation::LO);
    cart->clearCartridge(cartLocation::HI);
    cart->clearCartridge(cartLocation::HI_E000);

    const uint8_t romlBank = bank * 2;
    const uint8_t romhBank = romlBank + 1;

    const auto& sections = cart->getChipSections();

    for (const auto& sec : sections)
    {
        if (sec.data.size() != 8192)
            continue;

        if (sec.bankNumber == romlBank)
        {
            // ROML -> $8000
            for (size_t i = 0; i < 8192; ++i)
                cart->writeCartridge(static_cast<uint16_t>(i), sec.data[i], cartLocation::LO);

            if (mode == Mode::Freeze)
            {
                // Same ROML bank -> $E000 in Ultimax mode
                for (size_t i = 0; i < 8192; ++i)
                    cart->writeCartridge(static_cast<uint16_t>(i), sec.data[i], cartLocation::HI_E000);
            }
        }

        if (mode == Mode::Normal && normal16K && sec.bankNumber == romhBank)
        {
            // ROMH -> $A000
            for (size_t i = 0; i < 8192; ++i)
                cart->writeCartridge(static_cast<uint16_t>(i), sec.data[i], cartLocation::HI);
        }
    }

    return true;
}

const char* FreezeMachineMapper::getButtonName(uint32_t buttonIndex) const
{
    switch(buttonIndex)
    {
        case 0:
            return "Freeze";
        default:
            return "";
    }
}

void FreezeMachineMapper::pressButton(uint32_t buttonIndex)
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

bool FreezeMachineMapper::applyMappingAfterLoad()
{
    if (!cart)
        return false;

    switch (mode)
    {
        case Mode::Disabled:
            cart->setGameLine(true);
            cart->setExROMLine(true);
            break;

        case Mode::Normal:
            if (normal16K)
            {
                cart->setGameLine(false);
                cart->setExROMLine(false);
            }
            else
            {
                cart->setGameLine(true);
                cart->setExROMLine(false);
            }
            break;

        case Mode::Freeze:
            cart->setGameLine(false);
            cart->setExROMLine(true);
            break;
    }

    if (mode != Mode::Disabled)
        return loadIntoMemory(selectedHalf);

    return true;
}

void FreezeMachineMapper::pressFreeze()
{
    mode = Mode::Freeze;
    normal16K = false;

    cart->setGameLine(false);
    cart->setExROMLine(true);

    loadIntoMemory(selectedHalf);
}
