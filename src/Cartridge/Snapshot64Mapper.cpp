// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/Snapshot64Mapper.h"

Snapshot64Mapper::Snapshot64Mapper() :
    enabled(false)
{

}

Snapshot64Mapper::~Snapshot64Mapper() = default;

void Snapshot64Mapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("S64M");
    wrtr.writeU32(1); // Version

    wrtr.writeBool(enabled);

    wrtr.endChunk();
}

bool Snapshot64Mapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "S64M", 4) == 0)
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

void Snapshot64Mapper::reset()
{
    if (!cart)
        return;

    enabled = false;

    updateLines();
}

uint8_t Snapshot64Mapper::read(uint16_t address)
{
    if (!cart)
        return 0xFF;

    if (address >= 0xDF00 && address <= 0xDFFF)
        return 0x00;

    return cart->sampleDataBus();
}

void Snapshot64Mapper::write(uint16_t address, uint8_t value)
{
    (void)value;

    if (address >= 0xDF00 && address <= 0xDFFF)
    {
        enabled = false;
        updateLines();
    }
}

bool Snapshot64Mapper::loadIntoMemory(uint8_t bank)
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

        if (s.data.size() < 0x1000)
            continue;

        for (size_t i = 0; i < 0x2000; ++i)
        {
            const uint8_t value = s.data[i & 0x0FFF];

            cart->writeCartridge(static_cast<uint16_t>(i), value, cartLocation::LO);
            cart->writeCartridge(static_cast<uint16_t>(i), value, cartLocation::HI_E000);
        }

        return true;
    }

    return false;
}

const char* Snapshot64Mapper::getButtonName(uint32_t buttonIndex) const
{
    switch (buttonIndex)
    {
        case 0:
            return "Freeze";
        default:
            return "";
    }
}

void Snapshot64Mapper::pressButton(uint32_t buttonIndex)
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

bool Snapshot64Mapper::applyMappingAfterLoad()
{
    if (!cart)
        return false;

    if (!loadIntoMemory(0))
        return false;

    updateLines();

    return true;
}

void Snapshot64Mapper::pressFreeze()
{
    if (!cart)
        return;

    enabled = true;

    updateLines();

    cart->requestCartridgeNMI();
}

void Snapshot64Mapper::updateLines()
{
    if (!cart)
        return;

    if (enabled)
    {
        cart->setGameLine(false);
        cart->setExROMLine(true);
    }
    else
    {
        cart->setGameLine(true);
        cart->setExROMLine(true);
    }
}
