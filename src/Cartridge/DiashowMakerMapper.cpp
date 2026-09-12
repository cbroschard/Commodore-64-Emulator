// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/DiashowMakerMapper.h"

DiashowMakerMapper::DiashowMakerMapper() :
    enabled(true)
{

}

DiashowMakerMapper::~DiashowMakerMapper() = default;

void DiashowMakerMapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("DIAM");
    wrtr.writeU32(1); // Version

    wrtr.writeBool(enabled);

    wrtr.endChunk();
}

bool DiashowMakerMapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "DIAM", 4) == 0)
    {
        rdr.enterChunkPayload(chunk);

        uint32_t ver = 0;
        if (!rdr.readU32(ver))          { rdr.exitChunkPayload(chunk); return false; }
        if (ver != 1)                   { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readBool(enabled))     { rdr.exitChunkPayload(chunk); return false; }

        if (!applyMappingAfterLoad())   { rdr.exitChunkPayload(chunk); return false; }

        rdr.exitChunkPayload(chunk);
        return true;
    }

    return false;
}

void DiashowMakerMapper::reset()
{
    if (!cart)
        return;

    enabled = true;
    updateLines();
}

uint8_t DiashowMakerMapper::read(uint16_t address)
{
    if (!cart)
        return 0xFF;

    if (address == 0xDE00)
    {
        enabled = false;
        updateLines();
    }

    return cart->sampleDataBus();
}

void DiashowMakerMapper::write(uint16_t address, uint8_t value)
{
    (void)value;

    if (address == 0xDE00)
    {
        enabled = false;
        updateLines();
    }
}

bool DiashowMakerMapper::loadIntoMemory(uint8_t bank)
{
    if (!cart)
        return false;

    (void)bank;

    cart->clearCartridge(cartLocation::LO);
    cart->clearCartridge(cartLocation::HI);
    cart->clearCartridge(cartLocation::HI_E000);

    const auto& sections = cart->getChipSections();

    for (const auto& s : sections)
    {
        if (s.bankNumber != 0)
            continue;

        if (s.data.size() < 0x2000)
            continue;

        for (size_t i = 0; i < 0x2000; ++i)
            cart->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::LO);

        return true;
    }

    return false;
}

const char* DiashowMakerMapper::getButtonName(uint32_t buttonIndex) const
{
    switch (buttonIndex)
    {
        case 0:
            return "Freeze";
        default:
            return "";
    }
}

void DiashowMakerMapper::pressButton(uint32_t buttonIndex)
{
    switch (buttonIndex)
    {
        case 0:
            pressFreeze();
            break;
        default:
            break;
    }
}

bool DiashowMakerMapper::applyMappingAfterLoad()
{
     if (!cart)
        return false;

    if (!loadIntoMemory(0))
        return false;

    updateLines();

    return true;
}

void DiashowMakerMapper::pressFreeze()
{
    if (!cart)
        return;

    enabled = true;

    updateLines();

    cart->requestCartridgeNMI();
}

void DiashowMakerMapper::updateLines()
{
    if (!cart)
        return;

    cart->setGameLine(true);

    if (enabled)
        cart->setExROMLine(false); // 8K cartridge
    else
        cart->setExROMLine(true);  // cartridge hidden
}
