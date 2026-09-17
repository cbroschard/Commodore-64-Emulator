// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/Partner64Mapper.h"

Partner64Mapper::Partner64Mapper() :
    enabled(true)
{

}

Partner64Mapper::~Partner64Mapper() = default;

void Partner64Mapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("PR64");
    wrtr.writeU32(1); // Version

    wrtr.writeBool(enabled);

    wrtr.endChunk();
}

bool Partner64Mapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "PR64", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    uint32_t ver = 0;
    if (!rdr.readU32(ver))              { rdr.exitChunkPayload(chunk); return false; }
    if (ver != 1)                       { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readBool(enabled))         { rdr.exitChunkPayload(chunk); return false; }

    rdr.exitChunkPayload(chunk);
    return true;
}

void Partner64Mapper::reset()
{
    enabled = true;

    (void)applyMappingAfterLoad();
}

uint8_t Partner64Mapper::read(uint16_t address)
{
    if (!cart)
        return 0xFF;

    // Partner 64 cartridge RAM at $A000-$BFFF.
    if (enabled && address >= 0xA000 && address <= 0xBFFF)
        return cart->readRAM(static_cast<size_t>(address - 0xA000));

    // IO1 reads expose ROML $9E00-$9EFF.
    if (address >= 0xDE00 && address <= 0xDEFF)
    {
        const uint16_t offset = static_cast<uint16_t>(0x1E00 + (address & 0x00FF));
        return cart->readCartridge(offset, cartLocation::LO);
    }

    return cart->sampleDataBus();
}

void Partner64Mapper::write(uint16_t address, uint8_t value)
{
    if (!cart)
        return;

    // Partner 64 cartridge RAM at $A000-$BFFF.
    if (enabled && address >= 0xA000 && address <= 0xBFFF)
    {
        cart->writeRAM(static_cast<size_t>(address - 0xA000), value);
        return;
    }

    switch (address)
    {
        case 0xDE00:
        case 0xDEF0:
            enabled = false;
            updateLines();
            break;

        case 0xDEF1:
            enabled = true;
            updateLines();
            break;

        case 0xDEFF:
            enabled = true;
            updateLines();
            break;

        default:
            break;
    }
}

bool Partner64Mapper::loadIntoMemory(uint8_t bank)
{
    (void)bank;

    if (!cart)
        return false;

    cart->clearCartridge(cartLocation::LO);
    cart->clearCartridge(cartLocation::HI);
    cart->clearCartridge(cartLocation::HI_E000);

    bool loLoaded = false;
    bool hiLoaded = false;

    for (const auto& section : cart->getChipSections())
    {
        if (section.bankNumber != 0)
            continue;

        if (section.data.size() != 8192)
            continue;

        if (section.loadAddress == 0x8000)
        {
            for (size_t i = 0; i < 8192; ++i)
                cart->writeCartridge(static_cast<uint16_t>(i), section.data[i], cartLocation::LO);

            loLoaded = true;
        }
        else if (section.loadAddress == 0xE000)
        {
            for (size_t i = 0; i < 8192; ++i)
                cart->writeCartridge(static_cast<uint16_t>(i), section.data[i], cartLocation::HI_E000);

            hiLoaded = true;
        }
    }

    return loLoaded && hiLoaded;
}

bool Partner64Mapper::applyMappingAfterLoad()
{
    if (!cart)
        return false;

    updateLines();

    return loadIntoMemory(0);
}

const char* Partner64Mapper::getButtonName(uint32_t buttonIndex) const
{
    switch(buttonIndex)
    {
        case 0:
            return "Freeze";
        default:
            return "";
    }
}

void Partner64Mapper::pressButton(uint32_t buttonIndex)
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

bool Partner64Mapper::cpuReadHandledByMapper(uint16_t address) const
{
    return enabled && address >= 0xA000 && address <= 0xBFFF;
}

CartridgeWriteRoute Partner64Mapper::cpuWriteRoute(uint16_t address) const
{
    if (enabled && address >= 0xA000 && address <= 0xBFFF)
        return CartridgeWriteRoute::CartridgeOnly;

    return CartridgeWriteRoute::System;
}

void Partner64Mapper::updateLines()
{
    if (!cart)
        return;

    if (enabled)
    {
        // Normal Partner 64 active mapping.
        cart->setGameLine(false);
        cart->setExROMLine(true);
    }
    else
    {
        // Cartridge hidden.
        cart->setGameLine(true);
        cart->setExROMLine(true);
    }
}

void Partner64Mapper::pressFreeze()
{
    if (!cart)
        return;

    enabled = true;

    updateLines();

    cart->requestCartridgeNMI();
}
