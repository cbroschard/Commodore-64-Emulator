// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/UniversalCartridge1Mapper.h"

UniversalCartridge1Mapper::UniversalCartridge1Mapper() :
    ctrl{}
{

}

UniversalCartridge1Mapper::~UniversalCartridge1Mapper() = default;

void UniversalCartridge1Mapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("UC01");
    wrtr.writeU32(1);

    wrtr.writeU8(ctrl.bank);
    wrtr.writeBool(ctrl.ioDisabled);
    wrtr.writeBool(ctrl.sramWriteEnabled);
    wrtr.writeBool(ctrl.sramSelected);
    wrtr.writeBool(ctrl.gameHigh);
    wrtr.writeBool(ctrl.exromHigh);

    wrtr.endChunk();
}

bool UniversalCartridge1Mapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "UC01", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    uint32_t ver = 0;
    if (!rdr.readU32(ver))                      { rdr.exitChunkPayload(chunk); return false; }
    if (ver != 1)                               { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readU8(ctrl.bank))                 { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readBool(ctrl.ioDisabled))         { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readBool(ctrl.sramWriteEnabled))   { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(ctrl.sramSelected))       { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readBool(ctrl.gameHigh))           { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(ctrl.exromHigh))          { rdr.exitChunkPayload(chunk); return false; }

    rdr.exitChunkPayload(chunk);
    return true;
}

void UniversalCartridge1Mapper::reset()
{
    ctrl = UC1Control{};

    (void)applyMappingAfterLoad();
}

uint8_t UniversalCartridge1Mapper::read(uint16_t address)
{
    if (!cart)
        return 0xFF;

    if (ctrl.sramSelected)
    {
        if (address >= 0x8000 && address <= 0x9FFF)
            return cart->readRAM(ramLowOffset(address));

        if (address >= 0xA000 && address <= 0xBFFF)
            return cart->readRAM(ramHighOffset(address));

        // Ultimax high SRAM window.
        if (address >= 0xE000 && address <= 0xFFFF)
            return cart->readRAM(ramHighOffset(address));
    }

    return cart->sampleDataBus();
}

void UniversalCartridge1Mapper::write(uint16_t address, uint8_t value)
{
    if (!cart)
        return;

    // SRAM writes are independent of IO-register visibility.
    if (ctrl.sramWriteEnabled && getMode() != UC1Mode::Off)
    {
        if ((address >= 0x4000 && address <= 0x5FFF) || (address >= 0x8000 && address <= 0x9FFF))
        {
            cart->writeRAM(ramLowOffset(address), value);
            return;
        }

        if ((address >= 0x6000 && address <= 0x7FFF) || (address >= 0xA000 && address <= 0xBFFF) || (address >= 0xE000 && address <= 0xFFFF))
        {
            cart->writeRAM(ramHighOffset(address), value);
            return;
        }
    }

    // IO1 register is mirrored across $DE00-$DEFF.
    if (address >= 0xDE00 && address <= 0xDEFF)
    {
        if (!ctrl.ioDisabled)
        {
            ctrl = decodeControl(value);

            updateLines();
            (void)loadIntoMemory(ctrl.bank);
        }

        return;
    }
}

uint8_t UniversalCartridge1Mapper::peek(uint16_t address) const
{
    if (!cart)
        return 0xFF;

    if (ctrl.sramSelected)
    {
        if (address >= 0x8000 && address <= 0x9FFF)
            return cart->peekRAM(ramLowOffset(address));

        if (address >= 0xA000 && address <= 0xBFFF)
            return cart->peekRAM(ramHighOffset(address));

        if (address >= 0xE000 && address <= 0xFFFF)
            return cart->peekRAM(ramHighOffset(address));
    }

    return cart->sampleDataBus();
}

bool UniversalCartridge1Mapper::readDrivesBus(uint16_t address) const
{
    if (!cart)
        return false;

    if (!ctrl.sramSelected)
        return false;

    switch (getMode())
    {
        case UC1Mode::Mode16K:
            return address >= 0x8000 && address <= 0xBFFF;

        case UC1Mode::Mode8K:
            return address >= 0x8000 && address <= 0x9FFF;

        case UC1Mode::Ultimax:
            return (address >= 0x8000 && address <= 0x9FFF) || (address >= 0xE000 && address <= 0xFFFF);

        case UC1Mode::Off:
            return false;
    }

    return false;
}

bool UniversalCartridge1Mapper::loadIntoMemory(uint8_t bank)
{
    if (!cart)
        return false;

    cart->clearCartridge(cartLocation::LO);
    cart->clearCartridge(cartLocation::HI);
    cart->clearCartridge(cartLocation::HI_E000);

    bool loLoaded = false;
    bool hiLoaded = false;

    for (const auto& section : cart->getChipSections())
    {
        if (section.bankNumber != bank)
            continue;

        // Single 16K CHIP at $8000.
        if (section.loadAddress == 0x8000 && section.data.size() == 0x4000)
        {
            for (size_t i = 0; i < 0x2000; ++i)
            {
                cart->writeCartridge(static_cast<uint16_t>(i), section.data[i], cartLocation::LO);
                cart->writeCartridge(static_cast<uint16_t>(i), section.data[0x2000 + i], cartLocation::HI);
                cart->writeCartridge(static_cast<uint16_t>(i), section.data[0x2000 + i], cartLocation::HI_E000);
            }

            loLoaded = true;
            hiLoaded = true;
            continue;
        }

        // Separate 8K ROML CHIP.
        if (section.loadAddress == 0x8000 && section.data.size() == 0x2000)
        {
            for (size_t i = 0; i < 0x2000; ++i)
                cart->writeCartridge(static_cast<uint16_t>(i), section.data[i], cartLocation::LO);

            loLoaded = true;
            continue;
        }

        // Separate 8K ROMH CHIP.
        if ((section.loadAddress == 0xA000 || section.loadAddress == 0xE000) && section.data.size() == 0x2000)
        {
            for (size_t i = 0; i < 0x2000; ++i)
            {
                cart->writeCartridge(static_cast<uint16_t>(i), section.data[i], cartLocation::HI);
                cart->writeCartridge(static_cast<uint16_t>(i), section.data[i], cartLocation::HI_E000);
            }

            hiLoaded = true;
        }
    }

    return loLoaded && hiLoaded;
}

bool UniversalCartridge1Mapper::applyMappingAfterLoad()
{
    if (!cart)
        return false;

    ctrl.bank &= 0x07;

    updateLines();

    return loadIntoMemory(ctrl.bank);
}

bool UniversalCartridge1Mapper::cpuReadHandledByMapper(uint16_t address) const
{
    (void)address;
    return false;
}

CartridgeWriteRoute UniversalCartridge1Mapper::cpuWriteRoute(uint16_t address) const
{
    if (!ctrl.sramWriteEnabled)
        return CartridgeWriteRoute::System;

    if (getMode() == UC1Mode::Off)
        return CartridgeWriteRoute::System;

    if (address >= 0x4000 && address <= 0x7FFF)
        return CartridgeWriteRoute::CartridgeAndSystem;

    return CartridgeWriteRoute::System;
}

bool UniversalCartridge1Mapper::romReadHandledByMapper(uint16_t address) const
{
    if (!ctrl.sramSelected)
        return false;

    switch (getMode())
    {
        case UC1Mode::Mode16K:
            return address >= 0x8000 && address <= 0xBFFF;

        case UC1Mode::Mode8K:
            return address >= 0x8000 && address <= 0x9FFF;

        case UC1Mode::Ultimax:
            return (address >= 0x8000 && address <= 0x9FFF) ||
                   (address >= 0xE000 && address <= 0xFFFF);

        case UC1Mode::Off:
            return false;
    }

    return false;
}

bool UniversalCartridge1Mapper::romWriteEnabled(uint16_t address) const
{
    if (!ctrl.sramWriteEnabled)
        return false;

    switch (getMode())
    {
        case UC1Mode::Mode16K:
            return address >= 0x8000 && address <= 0xBFFF;

        case UC1Mode::Mode8K:
            return address >= 0x8000 && address <= 0x9FFF;

        case UC1Mode::Ultimax:
            return (address >= 0x8000 && address <= 0x9FFF) || (address >= 0xE000 && address <= 0xFFFF);

        case UC1Mode::Off:
            return false;
    }

    return false;
}

UniversalCartridge1Mapper::UC1Control UniversalCartridge1Mapper::decodeControl(uint8_t value) const
{
    UC1Control ctrl{};

    ctrl.bank             = value & 0x07;
    ctrl.ioDisabled       = (value & 0x08) != 0;
    ctrl.sramWriteEnabled = (value & 0x10) != 0;
    ctrl.sramSelected     = (value & 0x20) != 0;
    ctrl.gameHigh         = (value & 0x40) != 0;
    ctrl.exromHigh        = (value & 0x80) != 0;

    return ctrl;
}

UniversalCartridge1Mapper::UC1Mode UniversalCartridge1Mapper::getMode() const
{
    if (!ctrl.gameHigh && !ctrl.exromHigh)
        return UC1Mode::Mode16K;

    if (ctrl.gameHigh && !ctrl.exromHigh)
        return UC1Mode::Mode8K;

    if (!ctrl.gameHigh && ctrl.exromHigh)
        return UC1Mode::Ultimax;

    return UC1Mode::Off;
}

void UniversalCartridge1Mapper::updateLines()
{
    if (!cart)
        return;

    cart->setGameLine(ctrl.gameHigh);
    cart->setExROMLine(ctrl.exromHigh);
}

size_t UniversalCartridge1Mapper::ramLowOffset(uint16_t address) const
{
    return static_cast<size_t>((address & 0x1FFF) + ((ctrl.bank & 0x01) << 14));
}

size_t UniversalCartridge1Mapper::ramHighOffset(uint16_t address) const
{
    return static_cast<size_t>((address & 0x1FFF) + 0x2000 + ((ctrl.bank & 0x01) << 14));
}
