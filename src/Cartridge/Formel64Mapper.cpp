// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/Formel64Mapper.h"

Formel64Mapper::Formel64Mapper() :
    piaControlsMapping(false)
{
    pia.setPortBOutputCallback(
    [this](uint8_t value, uint8_t direction)
    {
        (void)value;
        (void)direction;

        if (piaControlsMapping)
        {
            updateLines();
            loadIntoMemory(romBank());
        }
    });
}

Formel64Mapper::~Formel64Mapper() = default;

void Formel64Mapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("FM64");
    wrtr.writeU32(1); // Version

    pia.save(wrtr);

    wrtr.writeBool(piaControlsMapping);

    wrtr.endChunk();
}

bool Formel64Mapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "FM64", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    uint32_t ver = 0;
    if (!rdr.readU32(ver))                  { rdr.exitChunkPayload(chunk); return false; }
    if (ver != 1)                           { rdr.exitChunkPayload(chunk); return false; }

    if (!pia.load(rdr))                     { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readBool(piaControlsMapping))  { rdr.exitChunkPayload(chunk); return false; }

    rdr.exitChunkPayload(chunk);
    return true;
}

void Formel64Mapper::reset()
{
    piaControlsMapping = false;

    pia.reset();

    if (!cart)
        return;

    cart->setGameLine(false);
    cart->setExROMLine(true);

    (void)loadIntoMemory(0);
}

uint8_t Formel64Mapper::read(uint16_t address)
{
    if (!cart)
        return 0xFF;

    switch (address)
    {
        case 0xDFC0:
            return pia.getPortADirection();

        case 0xDFC1:
            return pia.readPortARegister();

        case 0xDFC2:
            return pia.getPortBDirection();

        case 0xDFC3:
            return pia.readPortBRegister();
    }

    if (effectiveRomEnabled() && address >= 0xE000)
        return cart->readCartridge(address & 0x1FFF, cartLocation::HI_E000);

    return cart->sampleDataBus();
}

void Formel64Mapper::write(uint16_t address, uint8_t value)
{
    if (!cart)
        return;

    switch (address)
    {
        case 0xDFC0:
            pia.setPortADirection(value);
            break;

        case 0xDFC1:
            pia.writePortARegister(value);
            break;

        case 0xDFC2:
            pia.setPortBDirection(value);
            break;

        case 0xDFC3:
            piaControlsMapping = true;
            pia.writePortBRegister(value);
            break;
    }
}

uint8_t Formel64Mapper::peek(uint16_t address) const
{
    if (!cart)
        return 0xFF;

    switch (address)
    {
        case 0xDFC0:
            return pia.getPortADirection();

        case 0xDFC1:
            return pia.peekPortARegister();

        case 0xDFC2:
            return pia.getPortBDirection();

        case 0xDFC3:
            return pia.peekPortBRegister();
    }

    if (effectiveRomEnabled() && address >= 0xE000)
        return cart->readCartridge(address & 0x1FFF, cartLocation::HI_E000);

    return cart->sampleDataBus();
}

bool Formel64Mapper::loadIntoMemory(uint8_t bank)
{
    if (!cart)
        return false;

    bank &= 0x03;

    cart->clearCartridge(cartLocation::LO);
    cart->clearCartridge(cartLocation::HI);
    cart->clearCartridge(cartLocation::HI_E000);

    for (const auto& section : cart->getChipSections())
    {
        if (section.bankNumber != bank)
            continue;

        if (section.loadAddress != 0xE000)
            continue;

        if (section.data.size() != 0x2000)
            continue;

        for (size_t i = 0; i < 0x2000; ++i)
            cart->writeCartridge(static_cast<uint16_t>(i), section.data[i], cartLocation::HI_E000);

        return true;
    }

    return false;
}

bool Formel64Mapper::applyMappingAfterLoad()
{
    if (!cart)
        return false;

    updateLines();

    return loadIntoMemory(effectiveRomBank());
}

const char* Formel64Mapper::getButtonName(uint32_t buttonIndex) const
{
    switch(buttonIndex)
    {
        case 0:
            return "Reset";
        default:
            return "";
    }
}

void Formel64Mapper::pressButton(uint32_t buttonIndex)
{
    if (!cart)
        return;

    switch(buttonIndex)
    {
        case 0:
            cart->requestWarmReset();
            break;
        default:
            break;
    }
}

bool Formel64Mapper::cpuReadHandledByMapper(uint16_t address) const
{
    // MC6821 registers in IO2
    if (address >= 0xDFC0 && address <= 0xDFC3)
        return true;

    // Active ROMH in Ultimax
    if (effectiveRomEnabled() && address >= 0xE000)
        return true;

    return false;
}

CartridgeWriteRoute Formel64Mapper::cpuWriteRoute(uint16_t address) const
{
    if (address >= 0xDFC0 && address <= 0xDFC3)
        return CartridgeWriteRoute::CartridgeOnly;

    return CartridgeWriteRoute::System;
}

bool Formel64Mapper::readDrivesBus(uint16_t address) const
{
    if (address >= 0xDFC0 && address <= 0xDFC3)
        return true;

    if (effectiveRomEnabled() && address >= 0xE000)
        return true;

    return false;
}

void Formel64Mapper::updateLines()
{
    if (!cart)
        return;

    if (effectiveRomEnabled())
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
