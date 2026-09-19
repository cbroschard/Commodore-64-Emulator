// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/PagefoxMapper.h"

PagefoxMapper::PagefoxMapper() :
    ctrl{}
{

}

PagefoxMapper::~PagefoxMapper() = default;

void PagefoxMapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("PFOX");
    wrtr.writeU32(1); // Version

    wrtr.writeU8(ctrl.bankSelect);
    wrtr.writeU8(static_cast<uint8_t>(ctrl.chip));

    wrtr.writeBool(ctrl.enabled);

    wrtr.endChunk();
}

bool PagefoxMapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "PFOX", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    uint32_t ver = 0;
    if (!rdr.readU32(ver))                                  { rdr.exitChunkPayload(chunk); return false; }
    if (ver != 1)                                           { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readU8(ctrl.bankSelect))                       { rdr.exitChunkPayload(chunk); return false; }
    if (ctrl.bankSelect > 1)                                { rdr.exitChunkPayload(chunk); return false; }

    uint8_t chip = 0;
    if (!rdr.readU8(chip))                                  { rdr.exitChunkPayload(chunk); return false; }
    if (chip > static_cast<uint8_t>(PagefoxChip::Empty))    { rdr.exitChunkPayload(chunk); return false; }
    ctrl.chip = static_cast<PagefoxChip>(chip);

    if (!rdr.readBool(ctrl.enabled))                        { rdr.exitChunkPayload(chunk); return false; }

    rdr.exitChunkPayload(chunk);
    return true;
}

void PagefoxMapper::reset()
{
    ctrl = PagefoxControl{};

    (void)applyMappingAfterLoad();
}

uint8_t PagefoxMapper::read(uint16_t address)
{
    if (!cart)
        return 0xFF;

    if (!ctrl.enabled)
        return cart->sampleDataBus();

    switch (ctrl.chip)
    {
        case PagefoxChip::Eprom79:
        case PagefoxChip::EpromZS3:
            return cart->sampleDataBus();

        case PagefoxChip::RAM:
            if (address >= 0x8000 && address <= 0x9FFF)
                return cart->readRAM(ramLowOffset(address));

            if (address >= 0xA000 && address <= 0xBFFF)
                return cart->readRAM(ramHighOffset(address));

            break;

        case PagefoxChip::Empty:
            return cart->sampleDataBus();
    }

    return cart->sampleDataBus();
}

void PagefoxMapper::write(uint16_t address, uint8_t value)
{
    if (!cart)
        return;

    // Pagefox control register at $DE80-$DEFF.
    if (address >= 0xDE80 && address <= 0xDEFF)
    {
        decodeControl(value);
        updateLines();

        if (ctrl.chip == PagefoxChip::Eprom79 || ctrl.chip == PagefoxChip::EpromZS3)
            (void)loadIntoMemory(romBank());

        return;
    }

    // Pagefox RAM writes.
    if (ctrl.chip == PagefoxChip::RAM)
    {
        if (address >= 0x8000 && address <= 0x9FFF)
        {
            cart->writeRAM(ramLowOffset(address), value);
            return;
        }

        if (address >= 0xA000 && address <= 0xBFFF)
        {
            cart->writeRAM(ramHighOffset(address), value);
            return;
        }
    }
}

uint8_t PagefoxMapper::peek(uint16_t address) const
{
    if (!cart)
        return 0xFF;

    if (!ctrl.enabled)
        return cart->sampleDataBus();

    if (ctrl.chip == PagefoxChip::RAM)
    {
        if (address >= 0x8000 && address <= 0x9FFF)
            return cart->peekRAM(ramLowOffset(address));

        if (address >= 0xA000 && address <= 0xBFFF)
            return cart->peekRAM(ramHighOffset(address));
    }

    return cart->sampleDataBus();
}

bool PagefoxMapper::loadIntoMemory(uint8_t bank)
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

        if (section.loadAddress != 0x8000)
            continue;

        if (section.data.size() != 0x4000)
            continue;

        for (size_t i = 0; i < 0x2000; ++i)
        {
            cart->writeCartridge(static_cast<uint16_t>(i), section.data[i], cartLocation::LO);
            cart->writeCartridge(static_cast<uint16_t>(i), section.data[0x2000 + i], cartLocation::HI);
        }

        return true;
    }

    return false;
}

bool PagefoxMapper::applyMappingAfterLoad()
{
    if (!cart)
        return false;

    updateLines();

    if (ctrl.chip == PagefoxChip::Eprom79 || ctrl.chip == PagefoxChip::EpromZS3)
        return loadIntoMemory(romBank());

    return true;
}

bool PagefoxMapper::romReadHandledByMapper(uint16_t address) const
{
    if (!ctrl.enabled)
        return false;

    if (address < 0x8000 || address > 0xBFFF)
        return false;

    return ctrl.chip == PagefoxChip::RAM || ctrl.chip == PagefoxChip::Empty;
}

CartridgeWriteRoute PagefoxMapper::cpuWriteRoute(uint16_t address) const
{
    if (ctrl.chip == PagefoxChip::RAM &&
        address >= 0x8000 && address <= 0xBFFF)
    {
        return CartridgeWriteRoute::CartridgeAndSystem;
    }

    return CartridgeWriteRoute::System;
}

bool PagefoxMapper::readDrivesBus(uint16_t address) const
{
    if (!cart)
        return false;

    if (!ctrl.enabled)
        return false;

    if (address < 0x8000 || address > 0xBFFF)
        return false;

    switch (ctrl.chip)
    {
        case PagefoxChip::Eprom79:
        case PagefoxChip::EpromZS3:
            return true;

        case PagefoxChip::RAM:
            return true;

        case PagefoxChip::Empty:
            return false;
    }

    return false;
}

void PagefoxMapper::decodeControl(uint8_t value)
{
    ctrl.bankSelect = (value >> 1) & 0x01;
    ctrl.chip = static_cast<PagefoxChip>((value >> 2) & 0x03);
    ctrl.enabled = (value & 0x10) == 0;
}

uint8_t PagefoxMapper::romBank() const
{
    switch (ctrl.chip)
    {
        case PagefoxChip::Eprom79:
            return ctrl.bankSelect;

        case PagefoxChip::EpromZS3:
            return static_cast<uint8_t>(2 + ctrl.bankSelect);

        default:
            return 0;
    }
}

void PagefoxMapper::updateLines()
{
    if (!cart)
        return;

    if (ctrl.enabled)
    {
        cart->setGameLine(false);
        cart->setExROMLine(false);
    }
    else
    {
        cart->setGameLine(true);
        cart->setExROMLine(true);
    }
}

size_t PagefoxMapper::ramLowOffset(uint16_t address) const
{
    return static_cast<size_t>((ctrl.bankSelect * 0x4000) + (address & 0x1FFF));
}

size_t PagefoxMapper::ramHighOffset(uint16_t address) const
{
    return static_cast<size_t>((ctrl.bankSelect * 0x4000) + 0x2000 + (address & 0x1FFF));
}
