// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.H"
#include "Cartridge/SDBoxMapper.h"

SDBoxMapper::SDBoxMapper() :
    control(0x00),
    selectedBank(0),
    loadedBank(0xFF),
    sdReset(false),
    romEnabled(true),
    exROM(false),
    regEnabled(true),
    registerRAM{0, 0, 0}
{

}

SDBoxMapper::~SDBoxMapper() = default;

void SDBoxMapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("SDBX");
    wrtr.writeU32(1); // Version

    wrtr.writeU8(control);
    wrtr.writeU8(selectedBank);

    wrtr.writeBool(sdReset);
    wrtr.writeBool(romEnabled);
    wrtr.writeBool(exROM);
    wrtr.writeBool(regEnabled);

    for (int i = 0; i < 3; ++i)
        wrtr.writeU8(registerRAM[i]);

    wrtr.endChunk();
}

bool SDBoxMapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "SDBX", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    uint32_t ver = 0;
    if (!rdr.readU32(ver))                  { rdr.exitChunkPayload(chunk); return false; }
    if (ver != 1)                           { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readU8(control))               { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readU8(selectedBank))          { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readBool(sdReset))             { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(romEnabled))          { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(exROM))               { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(regEnabled))          { rdr.exitChunkPayload(chunk); return false; }

    for (int i = 0; i < 3; ++i)
        if (!rdr.readU8(registerRAM[i]))    { rdr.exitChunkPayload(chunk); return false; }

    loadedBank = 0xFF;

    rdr.exitChunkPayload(chunk);
    return true;


}

void SDBoxMapper::reset()
{
    control         = 0x00;

    selectedBank    = 0;
    loadedBank      = 0xFF;

    sdReset         = false;
    romEnabled      = true;
    exROM           = false;
    regEnabled      = true;

    for (int i = 0; i < 3; ++i)
        registerRAM[i] = 0;

   (void)applyMappingAfterLoad();
}

uint8_t SDBoxMapper::read(uint16_t address)
{
    if (!cart)
        return 0xFF;

    if (address >= 0xDE00 && address <= 0xDE03)
    {
        if (!regEnabled)
            return 0;

        if (address == 0xDE00)
            return control;

        return registerRAM[address - 0xDE01];
    }

    return cart->sampleDataBus();
}

void SDBoxMapper::write(uint16_t address, uint8_t value)
{
    if (!regEnabled)
        return;

    if (address == 0xDE00)
    {
        control = value;

        decodeControl(value);

        (void)applyMappingAfterLoad();
        return;
    }

    if (address >= 0xDE01 && address <= 0xDE03)
    {
        registerRAM[address - 0xDE01] = value;
        return;
    }
}

bool SDBoxMapper::loadIntoMemory(uint8_t bank)
{
    if (!cart)
        return false;

    cart->clearCartridge(cartLocation::LO);
    cart->clearCartridge(cartLocation::HI);
    cart->clearCartridge(cartLocation::HI_E000);

    for (const auto& section : cart->getChipSections())
    {
        if (section.bankNumber == bank && section.loadAddress == 0x8000 && section.data.size() == 0x4000)
        {
            for (size_t i = 0; i < 0x2000; ++i)
                cart->writeCartridge(static_cast<uint16_t>(i), section.data[i], cartLocation::LO);

            for (size_t i = 0; i < 0x2000; ++i)
                cart->writeCartridge(static_cast<uint16_t>(i), section.data[i + 0x2000], cartLocation::HI);

            updateLines();
            loadedBank = bank;
            return true;
        }
    }

    return false;
}

bool SDBoxMapper::applyMappingAfterLoad()
{
    if (!cart)
    return false;

    if (loadedBank != selectedBank)
        return loadIntoMemory(selectedBank);

    updateLines();

    return true;
}

void SDBoxMapper::updateLines()
{
    if (!cart)
        return;

    if (!exROM)
    {
        // 16 KB cartridge mode
        cart->setGameLine(false);
        cart->setExROMLine(false);
    }
    else
    {
        // Normal C64 memory
        cart->setGameLine(true);
        cart->setExROMLine(true);
    }
}

void SDBoxMapper::decodeControl(uint8_t value)
{
    selectedBank    = static_cast<uint8_t>(value & 0x0F);
    sdReset         = (value & 0x10) != 0;
    romEnabled      = (value & 0x20) != 0;
    exROM           = (value & 0x40) != 0;
    regEnabled      = (value & 0x80) == 0;
}
