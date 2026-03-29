// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/FreezeFrameMapper.h"

FreezeFrameMapper::FreezeFrameMapper() :
    mode(Mode::Normal)
{

}

FreezeFrameMapper::~FreezeFrameMapper() = default;

void FreezeFrameMapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("FZFM");
    wrtr.writeU32(1); // version

    wrtr.writeU8(static_cast<uint8_t>(mode));

    wrtr.endChunk();
}

bool FreezeFrameMapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "FZFM", 4) == 0)
    {
        rdr.enterChunkPayload(chunk);

        uint32_t ver = 0;
        if (!rdr.readU32(ver))          { rdr.exitChunkPayload(chunk); return false; }
        if (ver != 1)                   { rdr.exitChunkPayload(chunk); return false; }

        uint8_t modeU8 = 0;
        if (!rdr.readU8(modeU8))        { rdr.exitChunkPayload(chunk); return false; }

        mode = static_cast<Mode>(modeU8);

        rdr.exitChunkPayload(chunk);
        return true;
    }
    // Not our chunk
    return false;
}

uint8_t FreezeFrameMapper::read(uint16_t address)
{
    if (address >= 0xDE00 && address <= 0xDEFF)
        setMode(Mode::Normal);
    else if (address >= 0xDF00 && address <= 0xDFFF)
        setMode(Mode::Disabled);

    // Open bus
    return 0xFF;
}

void FreezeFrameMapper::write(uint16_t address, uint8_t value)
{
    (void)address;
    (void)value;
}

bool FreezeFrameMapper::loadIntoMemory(uint8_t bank)
{
    if (!cart || !mem)
        return false;

    (void)bank;

    const auto& sections = cart->getChipSections();

    for (const auto& sec : sections)
    {
        if (sec.data.size() == 8192 && sec.loadAddress == 0x8000)
        {
            if (mode == Mode::Normal || mode == Mode::Freeze)
            {
                for (size_t i = 0; i < 8192; ++i)
                    mem->writeCartridge(static_cast<uint16_t>(i), sec.data[i], cartLocation::LO);
            }

            if (mode == Mode::Freeze)
            {
                for (size_t i = 0; i < 8192; ++i)
                    mem->writeCartridge(static_cast<uint16_t>(i), sec.data[i], cartLocation::HI_E000);
            }

            return true;
        }
    }

    return false;
}

const char* FreezeFrameMapper::getButtonName(uint32_t buttonIndex) const
{
    switch(buttonIndex)
    {
        case 0:
            return "Freeze";
        default:
            return "";
    }
}

void FreezeFrameMapper::pressButton(uint32_t buttonIndex)
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

bool FreezeFrameMapper::applyMappingAfterLoad()
{
    return setMode(mode);
}

void FreezeFrameMapper::pressFreeze()
{
    setMode(Mode::Freeze);

    if (cart)
        cart->requestCartridgeNMI();
}

bool FreezeFrameMapper::setMode(Mode newMode)
{
    if (!cart || !mem)
        return false;

    mode = newMode;

    cart->clearCartridge(cartLocation::LO);
    cart->clearCartridge(cartLocation::HI);
    cart->clearCartridge(cartLocation::HI_E000);

    switch (mode)
    {
        case Mode::Disabled:
            cart->setExROMLine(true);
            cart->setGameLine(true);
            return true;

        case Mode::Normal:
            cart->setExROMLine(false);
            cart->setGameLine(true);
            return loadIntoMemory(0);

        case Mode::Freeze:
            cart->setExROMLine(true);
            cart->setGameLine(false);
            return loadIntoMemory(0);
    }

    return false;
}
