// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/RexRAMFloppyMapper.h"

RexRAMFloppyMapper::RexRAMFloppyMapper() :
    selectedBank(0),
    ramEnabled(false),
    ramWritable(false),
    enabled(true),
    switchEnabled(true)
{

}

RexRAMFloppyMapper::~RexRAMFloppyMapper() = default;

void RexRAMFloppyMapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("REXR");
    wrtr.writeU32(1); // Version

    wrtr.writeU8(selectedBank);

    wrtr.writeBool(ramEnabled);
    wrtr.writeBool(ramWritable);

    wrtr.writeBool(enabled);

    wrtr.writeBool(switchEnabled);

    wrtr.endChunk();
}

bool RexRAMFloppyMapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "REXR", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    uint32_t ver = 0;
    if (!rdr.readU32(ver))              { rdr.exitChunkPayload(chunk); return false; }
    if (ver != 1)                       { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readU8(selectedBank))      { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readBool(ramEnabled))      { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(ramWritable))     { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readBool(enabled))         { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readBool(switchEnabled))   { rdr.exitChunkPayload(chunk); return false; }

    rdr.exitChunkPayload(chunk);

    return true;
}

void RexRAMFloppyMapper::reset()
{
    selectedBank    = 0;

    ramEnabled      = false;
    ramWritable     = false;

    enabled         = true;

    (void)applyMappingAfterLoad();
}

uint8_t RexRAMFloppyMapper::read(uint16_t address)
{
    if (!cart)
        return 0xFF;

    switch (address)
    {
        case 0xDF50:
            ramWritable = !ramWritable;
            return 0;

        case 0xDFC0:
            enabled = !enabled;
            updateLines();
            return 0;

        case 0xDFE0:
            ramEnabled = !ramEnabled;
            return 0;
    }

    if (address >= 0x8000 && address <= 0x9FFF)
    {
        if (ramEnabled)
        {
            const size_t offset = static_cast<size_t>(selectedBank) * 0x2000 + (address & 0x1FFF);
            return cart->readRAM(offset);
        }

        return cart->readCartridge(address - 0x8000, cartLocation::LO);
    }

    return cart->sampleDataBus();
}

void RexRAMFloppyMapper::write(uint16_t address, uint8_t value)
{
    if (!cart)
        return;

    if (address == 0xDFA0)
    {
        selectedBank = static_cast<uint8_t>((value & 0x07) |((value & 0x30) >> 1));
        return;
    }

    if (address >= 0x8000 && address <= 0x9FFF)
    {
        if (!enabled || !ramEnabled || !ramWritable)
            return;

        const size_t offset = static_cast<size_t>(selectedBank) * 0x2000 + (address & 0x1FFF);

        cart->writeRAM(offset, value);
    }
}

bool RexRAMFloppyMapper::loadIntoMemory(uint8_t bank)
{
    if (!cart)
        return false;

    (void)bank;

    cart->clearCartridge(cartLocation::LO);
    cart->clearCartridge(cartLocation::HI);
    cart->clearCartridge(cartLocation::HI_E000);

    for (const auto& section : cart->getChipSections())
    {
        if (section.loadAddress == 0x8000 && section.data.size() == 0x2000)
        {
            // $8000-$9FFF
            for (size_t i = 0; i < 0x2000; ++i)
                cart->writeCartridge(static_cast<uint16_t>(i), section.data[i], cartLocation::LO);

            return true;
        }
    }

    return false;
}

bool RexRAMFloppyMapper::applyMappingAfterLoad()
{
    if (!cart)
        return false;

    if (!loadIntoMemory(0))
        return false;

    updateLines();
    return true;
}

bool RexRAMFloppyMapper::romReadHandledByMapper(uint16_t address) const
{
    return ramEnabled && address >= 0x8000 && address <= 0x9FFF;
}

CartridgeWriteRoute RexRAMFloppyMapper::cpuWriteRoute(uint16_t address) const
{
    if (enabled && switchEnabled && ramEnabled && ramWritable && address >= 0x8000 && address <= 0x9FFF)
        return CartridgeWriteRoute::CartridgeOnly;

    return CartridgeWriteRoute::System;
}

bool RexRAMFloppyMapper::hasPersistence() const
{
    return true;
}

bool RexRAMFloppyMapper::savePersistence(const std::string& path) const
{
    if (!cart || !cart->hasCartridgeRAM())
        return false;

    std::ofstream file(path, std::ios::binary | std::ios::trunc);

    if (!file)
        return false;

    for (size_t i = 0; i < cart->ramSize(); ++i)
    {
        const uint8_t value = cart->peekRAM(i);
        file.put(static_cast<char>(value));

        if (!file)
            return false;
    }

    return true;
}

bool RexRAMFloppyMapper::loadPersistence(const std::string& path)
{
    if (!cart || !cart->hasCartridgeRAM())
        return false;

    std::ifstream file(path, std::ios::binary);

    if (!file)
        return false;

    for (size_t i = 0; i < cart->ramSize(); ++i)
    {
        char value = 0;

        if (!file.get(value))
            return false;

        cart->writeRAM(i, static_cast<uint8_t>(static_cast<unsigned char>(value)));
    }

    return true;
}

uint32_t RexRAMFloppyMapper::getSwitchCount() const
{
    return 1;
}

const char* RexRAMFloppyMapper::getSwitchName(uint32_t switchIndex) const
{
    return switchIndex == 0 ? "Cartridge" : "";
}

uint32_t RexRAMFloppyMapper::getSwitchPositionCount(uint32_t switchIndex) const
{
    return switchIndex == 0 ? 2 : 0;
}

uint32_t RexRAMFloppyMapper::getSwitchPosition(uint32_t switchIndex) const
{
    if (switchIndex != 0)
        return 0;

    return switchEnabled ? 1 : 0;
}

const char* RexRAMFloppyMapper::getSwitchPositionLabel(uint32_t switchIndex, uint32_t pos) const
{
    if (switchIndex != 0)
        return "";

    switch (pos)
    {
        case 0: return "Off";
        case 1: return "On";
        default: return "";
    }
}

void RexRAMFloppyMapper::setSwitchPosition(uint32_t switchIndex, uint32_t pos)
{
    if (switchIndex != 0 || pos > 1)
        return;

    switchEnabled = (pos == 1);
    updateLines();
}

void RexRAMFloppyMapper::updateLines()
{
    if (!cart)
        return;

    const bool active = enabled && switchEnabled;

    cart->setGameLine(true);
    cart->setExROMLine(!active);
}
