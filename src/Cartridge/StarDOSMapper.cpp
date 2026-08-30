// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include <algorithm>
#include "Cartridge.h"
#include "Cartridge/StarDOSMapper.h"

StarDOSMapper::StarDOSMapper() :
    io1Charge(0),
    io2Charge(0),
    romlEnabled(true),
    loaded(false)
{

}

StarDOSMapper::~StarDOSMapper() = default;

void StarDOSMapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("STAR");
    wrtr.writeU32(1);

    wrtr.writeU16(io1Charge);
    wrtr.writeU16(io2Charge);

    wrtr.writeBool(romlEnabled);
    wrtr.writeBool(loaded);

    wrtr.endChunk();
}

bool StarDOSMapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "STAR", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    uint32_t version = 0;

    if (!rdr.readU32(version))              { rdr.exitChunkPayload(chunk); return false; }

    if (version != 1)
    {
        rdr.exitChunkPayload(chunk);
        return false;
    }

    if (!rdr.readU16(io1Charge) ||
        !rdr.readU16(io2Charge) ||
        !rdr.readBool(romlEnabled) ||
        !rdr.readBool(loaded))              { rdr.exitChunkPayload(chunk); return false; }

    if (io1Charge > SWITCH_THRESHOLD ||
        io2Charge > SWITCH_THRESHOLD)       { rdr.exitChunkPayload(chunk); return false; }

    rdr.exitChunkPayload(chunk);

    return true;
}

uint8_t StarDOSMapper::read(uint16_t address)
{
    if (!cart)
        return 0xFF;

    if (address >= 0xE000)
    {
        if (!loaded)
            return cart->sampleDataBus();

        return cart->readCartridge(static_cast<uint16_t>(address - 0xE000), cartLocation::HI_E000);
    }

    if (address >= 0xDE00 && address <= 0xDEFF)
    {
        chargeIO1();
        return cart->sampleDataBus();
    }

    if (address >= 0xDF00 && address <= 0xDFFF)
    {
        chargeIO2();
        return cart->sampleDataBus();
    }

    return cart->sampleDataBus();
}

void StarDOSMapper::write(uint16_t address, uint8_t value)
{
    (void)address;
    (void)value;
}

bool StarDOSMapper::loadIntoMemory(uint8_t bank)
{
    (void)bank;

    if (!cart)
        return false;

    const Cartridge::chipSection* loSection = nullptr;
    const Cartridge::chipSection* kernalSection = nullptr;

    for (const auto& section : cart->getChipSections())
    {
        if (section.data.size() != 0x2000)
            continue;

        if (section.loadAddress == 0x8000)
            loSection = &section;
        else if (section.loadAddress == 0xE000)
            kernalSection = &section;
    }

    if (!loSection || !kernalSection)
    {
        std::cerr
            << "StarDOS: Required $8000 or $E000 ROM "
               "section is missing.\n";

        loaded = false;
        return false;
    }

    cart->clearCartridge(cartLocation::LO);
    cart->clearCartridge(cartLocation::HI_E000);

    for (size_t i = 0; i < 0x2000; ++i)
    {
        cart->writeCartridge(
            static_cast<uint16_t>(i),
            loSection->data[i],
            cartLocation::LO);

        cart->writeCartridge(
            static_cast<uint16_t>(i),
            kernalSection->data[i],
            cartLocation::HI_E000);
    }

    loaded = true;
    return true;
}

void StarDOSMapper::tick(uint32_t elapsedCycles)
{
    if (elapsedCycles >= io1Charge)
        io1Charge = 0;
    else
        io1Charge = static_cast<uint16_t>(io1Charge - elapsedCycles);

    if (elapsedCycles >= io2Charge)
        io2Charge = 0;
    else
        io2Charge = static_cast<uint16_t>(io2Charge - elapsedCycles);
}

void StarDOSMapper::chargeIO1()
{
    const uint32_t charged =
        static_cast<uint32_t>(io1Charge) +
        CHARGE_INCREMENT;

    io1Charge = static_cast<uint16_t>(std::min<uint32_t>(charged, SWITCH_THRESHOLD));

    if (io1Charge >= SWITCH_THRESHOLD)
    {
        io1Charge = 0;
        io2Charge = 0;

        romlEnabled = true;
        applyLineState();
    }
}

void StarDOSMapper::chargeIO2()
{
    const uint32_t charged =
        static_cast<uint32_t>(io2Charge) +
        CHARGE_INCREMENT;

    io2Charge = static_cast<uint16_t>(std::min<uint32_t>(charged, SWITCH_THRESHOLD));

    if (io2Charge >= SWITCH_THRESHOLD)
    {
        io1Charge = 0;
        io2Charge = 0;

        romlEnabled = false;
        applyLineState();
    }
}

void StarDOSMapper::applyLineState()
{
    if (!cart)
        return;

    cart->setExROMLine(true);

    if (romlEnabled)
    {
        // StarDOS startup mapping:
        // GAME low, EXROM high.
        cart->setGameLine(false);
    }
    else
    {
        // Cartridge ROML removed.
        cart->setGameLine(true);
    }
}

bool StarDOSMapper::applyMappingAfterLoad()
{
    if (!cart)
        return false;

    if (!loadIntoMemory(0))
        return false;

    applyLineState();
    return true;
}

bool StarDOSMapper::readDrivesBus(uint16_t address) const
{
    if (!cart || !loaded)
        return false;

    return address >= 0xE000;
}

bool StarDOSMapper::romReadHandledByMapper(uint16_t address) const
{
    return loaded && address >= 0xE000;
}
