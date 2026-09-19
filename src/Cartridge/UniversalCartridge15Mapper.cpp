// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/UniversalCartridge15Mapper.h"

UniversalCartridge15Mapper::UniversalCartridge15Mapper() :
    ctrl{}
{

}

UniversalCartridge15Mapper::~UniversalCartridge15Mapper() = default;

void UniversalCartridge15Mapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("UC15");
    wrtr.writeU32(1); // Version

    wrtr.writeU8(ctrl.bank);
    wrtr.writeBool(ctrl.ioDisabled);
    wrtr.writeBool(ctrl.sramWriteEnabled);
    wrtr.writeBool(ctrl.sramSelected);
    wrtr.writeBool(ctrl.gameHigh);
    wrtr.writeBool(ctrl.exromHigh);

    wrtr.endChunk();
}

bool UniversalCartridge15Mapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "UC15", 4) != 0)
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

void UniversalCartridge15Mapper::reset()
{
    ctrl = UC15Control{};

    (void)applyMappingAfterLoad();
}

uint8_t UniversalCartridge15Mapper::read(uint16_t address)
{
    if (!cart)
        return 0xFF;

    if (ctrl.sramSelected)
    {
        if (address >= 0x8000 && address <= 0x9FFF)
            return cart->readRAM(ramLowOffset(address));

        if (address >= 0xA000 && address <= 0xBFFF)
            return cart->readRAM(ramHighOffset(address));

        if (address >= 0xE000 && address <= 0xFFFF)
            return cart->readRAM(ramHighOffset(address));
    }

    // IO1 / IO2 expose the top 512 bytes of the selected 16K SRAM bank.
    if (address >= 0xDE00 && address <= 0xDFFF)
        return cart->readRAM(ioRamOffset(address));

    return cart->sampleDataBus();
}

void UniversalCartridge15Mapper::write(uint16_t address, uint8_t value)
{
    if (!cart)
        return;

    if (!ctrl.ioDisabled && address >= 0xDE00 && address <= 0xDEFF)
    {
        switch (address & 0x03)
        {
            case 2:
                decodeBank(value);
                (void)loadIntoMemory(ctrl.bank);
                return;

            case 3:
                decodeControl(value);
                updateLines();
                return;

            default:
                break;
        }
    }

    // IO1 / IO2 SRAM writes.
    // Writing through the IO area works only when SRAM writes are enabled
    // and EPROM is selected.
    if (ctrl.sramWriteEnabled && !ctrl.sramSelected)
    {
        // IO2 is always available for SRAM writes.
        if (address >= 0xDF00 && address <= 0xDFFF)
        {
            cart->writeRAM(ioRamOffset(address), value);
            return;
        }

        // IO1 becomes SRAM-writable once the UC control registers
        // have been disabled.
        if (ctrl.ioDisabled && address >= 0xDE00 && address <= 0xDEFF)
        {
            cart->writeRAM(ioRamOffset(address), value);
            return;
        }
    }

    // SRAM writes are independent of control-register visibility.
    if (ctrl.sramWriteEnabled)
    {
        if ((address >= 0x4000 && address <= 0x5FFF) || (address >= 0x8000 && address <= 0x9FFF))
        {
            cart->writeRAM(ramLowOffset(address), value);
            return;
        }

        if ((address >= 0x6000 && address <= 0x7FFF) || (address >= 0xA000 && address <= 0xBFFF))
        {
            cart->writeRAM(ramHighOffset(address), value);
            return;
        }
    }
}

uint8_t UniversalCartridge15Mapper::peek(uint16_t address) const
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

    // IO1 / IO2 expose the top 512 bytes of the selected SRAM bank.
    if (address >= 0xDE00 && address <= 0xDFFF)
        return cart->peekRAM(ioRamOffset(address));

    return cart->sampleDataBus();
}

bool UniversalCartridge15Mapper::readDrivesBus(uint16_t address) const
{
    if (!cart)
        return false;

    // IO1 / IO2 SRAM always drives reads from the IO SRAM window.
    if (address >= 0xDE00 && address <= 0xDFFF)
        return true;

    if (!ctrl.sramSelected)
        return false;

    switch (getMode())
    {
        case UC15Mode::Mode16K:
            return address >= 0x8000 && address <= 0xBFFF;

        case UC15Mode::Mode8K:
            return address >= 0x8000 && address <= 0x9FFF;

        case UC15Mode::Ultimax:
            return (address >= 0x8000 && address <= 0x9FFF) || (address >= 0xE000 && address <= 0xFFFF);

        case UC15Mode::Off:
            return false;
    }

    return false;
}

bool UniversalCartridge15Mapper::loadIntoMemory(uint8_t bank)
{
    if (!cart)
        return false;

    bank &= 0x1F;

    cart->clearCartridge(cartLocation::LO);
    cart->clearCartridge(cartLocation::HI);
    cart->clearCartridge(cartLocation::HI_E000);

    bool loLoaded = false;
    bool hiLoaded = false;

    for (const auto& section : cart->getChipSections())
    {
        if (section.bankNumber != bank)
            continue;

        if (section.data.size() != 0x2000)
            continue;

        if (section.loadAddress == 0x8000)
        {
            for (size_t i = 0; i < 0x2000; ++i)
                cart->writeCartridge(static_cast<uint16_t>(i), section.data[i], cartLocation::LO);

            loLoaded = true;
        }
        else if (section.loadAddress == 0xA000)
        {
            for (size_t i = 0; i < 0x2000; ++i)
            {
                cart->writeCartridge(static_cast<uint16_t>(i), section.data[i], cartLocation::HI);

                // Same upper 8K appears at $E000 in Ultimax mode.
                cart->writeCartridge(static_cast<uint16_t>(i), section.data[i], cartLocation::HI_E000);
            }

            hiLoaded = true;
        }
    }

    return loLoaded && hiLoaded;
}

bool UniversalCartridge15Mapper::applyMappingAfterLoad()
{
    if (!cart)
        return false;

    ctrl.bank &= 0x1F;

    updateLines();

    return loadIntoMemory(ctrl.bank);
}

bool UniversalCartridge15Mapper::cpuReadHandledByMapper(uint16_t address) const
{
    (void)address;
    return false;
}

CartridgeWriteRoute UniversalCartridge15Mapper::cpuWriteRoute(uint16_t address) const
{
    if (!ctrl.sramWriteEnabled)
        return CartridgeWriteRoute::System;

    if (address >= 0x4000 && address <= 0xBFFF)
        return CartridgeWriteRoute::CartridgeAndSystem;

    return CartridgeWriteRoute::System;
}

bool UniversalCartridge15Mapper::romReadHandledByMapper(uint16_t address) const
{
    if (!ctrl.sramSelected)
        return false;

    switch (getMode())
    {
        case UC15Mode::Mode16K:
            return address >= 0x8000 && address <= 0xBFFF;

        case UC15Mode::Mode8K:
            return address >= 0x8000 && address <= 0x9FFF;

        case UC15Mode::Ultimax:
            return (address >= 0x8000 && address <= 0x9FFF) ||
                   (address >= 0xE000 && address <= 0xFFFF);

        case UC15Mode::Off:
            return false;
    }

    return false;
}

void UniversalCartridge15Mapper::decodeBank(uint8_t value)
{
    ctrl.bank = value & 0x1F;
}

void UniversalCartridge15Mapper::decodeControl(uint8_t value)
{
    ctrl.ioDisabled       = (value & 0x08) != 0;
    ctrl.sramWriteEnabled = (value & 0x10) != 0;
    ctrl.sramSelected     = (value & 0x20) != 0;
    ctrl.gameHigh         = (value & 0x40) != 0;
    ctrl.exromHigh        = (value & 0x80) != 0;
}

UniversalCartridge15Mapper::UC15Mode UniversalCartridge15Mapper::getMode() const
{
    if (!ctrl.gameHigh && !ctrl.exromHigh)
        return UC15Mode::Mode16K;

    if (ctrl.gameHigh && !ctrl.exromHigh)
        return UC15Mode::Mode8K;

    if (!ctrl.gameHigh && ctrl.exromHigh)
        return UC15Mode::Ultimax;

    return UC15Mode::Off;
}

void UniversalCartridge15Mapper::updateLines()
{
    if (!cart)
        return;

    cart->setGameLine(ctrl.gameHigh);
    cart->setExROMLine(ctrl.exromHigh);
}

size_t UniversalCartridge15Mapper::ramLowOffset(uint16_t address) const
{
    return static_cast<size_t>((address & 0x1FFF) + ((ctrl.bank & 0x01) << 14));
}

size_t UniversalCartridge15Mapper::ramHighOffset(uint16_t address) const
{
    return static_cast<size_t>((address & 0x1FFF) + 0x2000 + ((ctrl.bank & 0x01) << 14));
}

size_t UniversalCartridge15Mapper::ioRamOffset(uint16_t address) const
{
    const size_t bankBase = (ctrl.bank & 0x01) ? 0x4000 : 0x0000;
    return bankBase + 0x3E00 + static_cast<size_t>(address & 0x01FF);
}
