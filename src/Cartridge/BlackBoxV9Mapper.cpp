// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/BlackBoxV9Mapper.h"

BlackBoxV9Mapper::BlackBoxV9Mapper() :
    selectedBank(1),
    loadedBank(0xFF),
    gameHigh(false),
    exromHigh(true)
{

}

BlackBoxV9Mapper::~BlackBoxV9Mapper() = default;

void BlackBoxV9Mapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("BBV9");
    wrtr.writeU32(1);

    wrtr.writeU8(selectedBank);
    wrtr.writeBool(gameHigh);
    wrtr.writeBool(exromHigh);

    wrtr.endChunk();
}

bool BlackBoxV9Mapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "BBV9", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    uint32_t ver = 0;
    if (!rdr.readU32(ver))                  { rdr.exitChunkPayload(chunk); return false; }
    if (ver != 1)                           { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readU8(selectedBank))          { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readBool(gameHigh))            { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(exromHigh))           { rdr.exitChunkPayload(chunk); return false; }

    selectedBank &= 0x01;

    // Force ROM window rebuild after state load.
    loadedBank = 0xFF;

    rdr.exitChunkPayload(chunk);
    return true;
}

void BlackBoxV9Mapper::reset()
{
    selectedBank    = 1;
    loadedBank      = 0xFF;
    gameHigh        = false;
    exromHigh       = true;

    (void)applyMappingAfterLoad();
}

uint8_t BlackBoxV9Mapper::read(uint16_t address)
{
    if (!cart)
        return 0xFF;

    if (address >= 0xDE00 && address <= 0xDEFF)
    {
        decodeControl(address, false);

        if (!applyMappingAfterLoad())
            return cart->sampleDataBus();

        const uint16_t offset = static_cast<uint16_t>(0x1E00u + (address & 0x00FF));

        return cart->readCartridge(offset, cartLocation::HI_E000);
    }

    return cart->sampleDataBus();
}

void BlackBoxV9Mapper::write(uint16_t address, uint8_t value)
{
    (void)value;

    if (!cart)
        return;

    if (address < 0xDE00 || address > 0xDEFF)
        return;

    decodeControl(address, true);

    (void)applyMappingAfterLoad();
}

uint8_t BlackBoxV9Mapper::peek(uint16_t address) const
{
    if (!cart)
        return 0xFF;

    if (address < 0xDE00 || address > 0xDEFF)
        return cart->sampleDataBus();

    // Reads use A7 directly for bank selection.
    const uint8_t bank = static_cast<uint8_t>((address >> 7) & 0x01);

    // Second-last page of the 16K bank.
    const size_t offset = 0x3E00u + static_cast<size_t>(address & 0x00FF);

    for (const auto& section : cart->getChipSections())
    {
        if (section.bankNumber != bank)
            continue;

        if (section.loadAddress != 0x8000)
            continue;

        if (offset < section.data.size())
            return section.data[offset];
    }

    return cart->sampleDataBus();
}

bool BlackBoxV9Mapper::loadIntoMemory(uint8_t bank)
{
    if (!cart)
        return false;

    bank &= 0x01;

    cart->clearCartridge(cartLocation::LO);
    cart->clearCartridge(cartLocation::HI);
    cart->clearCartridge(cartLocation::HI_E000);

    for (const auto& section : cart->getChipSections())
    {
        if (section.bankNumber != bank)
            continue;

        if (section.loadAddress != 0x8000)
            continue;

        if (section.data.size() < 0x4000)
            continue;

        // First half -> ROML $8000-$9FFF
        for (size_t i = 0; i < 0x2000; ++i)
            cart->writeCartridge(static_cast<uint16_t>(i), section.data[i], cartLocation::LO);

        // Second half -> ROMH $E000-$FFFF
        for (size_t i = 0; i < 0x2000; ++i)
            cart->writeCartridge(static_cast<uint16_t>(i), section.data[0x2000 + i], cartLocation::HI_E000);

        return true;
    }

    return false;
}

bool BlackBoxV9Mapper::readDrivesBus(uint16_t address) const
{
    return cart && address >= 0xDE00 && address <= 0xDEFF;
}

void BlackBoxV9Mapper::decodeControl(uint16_t address, bool isWrite)
{
    const uint8_t ioAddress = static_cast<uint8_t>(address & 0x00FF);

    // A7 selects one of the two 16K banks.
    uint8_t bank = static_cast<uint8_t>((ioAddress >> 7) & 0x01);

    // Bank selection is inverted on writes.
    if (isWrite)
        bank ^= 0x01;

    selectedBank = bank;

    // Direct physical line levels.
    exromHigh = (ioAddress & 0x40) != 0;
    gameHigh  = (ioAddress & 0x01) != 0;
}

bool BlackBoxV9Mapper::applyMappingAfterLoad()
{
    if (!cart)
        return false;

    if (loadedBank != selectedBank)
    {
        if (!loadIntoMemory(selectedBank))
            return false;

        loadedBank = selectedBank;
    }

    updateLines();

    return true;
}

void BlackBoxV9Mapper::updateLines()
{
    if (!cart)
        return;

    cart->setGameLine(gameHigh);
    cart->setExROMLine(exromHigh);
}
