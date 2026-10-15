// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/GameKillerMapper.h"

GameKillerMapper::GameKillerMapper() :
    enabled(true),
    disableWrites(0)
{

}

GameKillerMapper::~GameKillerMapper() = default;

void GameKillerMapper::reset()
{
    enabled         = true;
    disableWrites   = 0;

    (void)applyMappingAfterLoad();
}

void GameKillerMapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("GKLR");
    wrtr.writeU32(2); // Version

    wrtr.writeBool(enabled);
    wrtr.writeU8(disableWrites);

    wrtr.endChunk();
}

bool GameKillerMapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "GKLR", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    uint32_t ver = 0;
    if (!rdr.readU32(ver))          { rdr.exitChunkPayload(chunk); return false; }
    if (ver !=1)                    { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readBool(enabled))     { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readU8(disableWrites)) { rdr.exitChunkPayload(chunk); return false; }

    updateLines();

    rdr.exitChunkPayload(chunk);

    return true;
}

uint8_t GameKillerMapper::read(uint16_t address)
{
    (void)address;
    return cart ? cart->sampleDataBus() : 0xFF;
}

void GameKillerMapper::write(uint16_t address, uint8_t value)
{
    (void)value;

    if (address < 0xDE00 || address > 0xDFFF)
        return;

    if (++disableWrites >= 2)
    {
        disableWrites = 0;
        enabled = false;

        updateLines();
    }
}

bool GameKillerMapper::loadIntoMemory(uint8_t bank)
{
    (void)bank;

    if (!cart)
        return false;

    cart->clearCartridge(cartLocation::LO);
    cart->clearCartridge(cartLocation::HI);
    cart->clearCartridge(cartLocation::HI_E000);

    for (const auto& section : cart->getChipSections())
    {
        // Loads into 0xE000 with Ultimax Enabled
        if (section.loadAddress == 0xE000 && section.data.size() == 0x2000)
        {
            for (size_t i = 0; i < 0x2000; ++i)
                cart->writeCartridge(static_cast<uint16_t>(i), section.data[i], cartLocation::HI_E000);

            updateLines();
            return true;
        }
    }

    return false;
}

bool GameKillerMapper::applyMappingAfterLoad()
{
    if (!cart)
        return false;

    if (!loadIntoMemory(0))
        return false;

    updateLines();
    return true;
}

uint32_t GameKillerMapper::getButtonCount() const
{
    return 1;
}

const char* GameKillerMapper::getButtonName(uint32_t buttonIndex) const
{
    switch(buttonIndex)
    {
        case 0:
            return "Freeze";
        default:
            return "";
    }
}

void GameKillerMapper::pressButton(uint32_t index)
{
    switch(index)
    {
        case 0:
            pressFreeze();
            break;
        default:
            break;
    }
}

CartridgeWriteRoute GameKillerMapper::cpuWriteRoute(uint16_t address) const
{
    if (address >= 0xDE00 && address <= 0xDFFF)
        return CartridgeWriteRoute::CartridgeOnly;

    return CartridgeWriteRoute::System;
}

void GameKillerMapper::updateLines()
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

void GameKillerMapper::pressFreeze()
{
    if (!cart)
        return;

    enabled = true;
    disableWrites = 0;

    updateLines();

    cart->requestCartridgeNMI();
}
