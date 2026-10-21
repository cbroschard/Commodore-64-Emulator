// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/IEEE488Mapper.h"
#include "IEEE488/IEEE488Bus.h"

IEEE488Mapper::IEEE488Mapper() :
    ieee488Bus(nullptr)
{

}

IEEE488Mapper::~IEEE488Mapper() = default;

void IEEE488Mapper::saveState(StateWriter& wrtr) const
{

}

bool IEEE488Mapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{

}

void IEEE488Mapper::reset()
{
    tpi.reset();
}

uint8_t IEEE488Mapper::read(uint16_t address)
{
    if (address >= 0xDF00 && address <= 0xDF07)
    {
        const uint8_t reg = static_cast<uint8_t>(address & 0x07);
        return tpi.readRegister(reg);
    }

    if (!cart)
        return 0xFF;

    return cart->sampleDataBus();
}

void IEEE488Mapper::write(uint16_t address, uint8_t value)
{
    if (address >= 0xDF00 && address <= 0xDF07)
        tpi.writeRegister(static_cast<uint8_t>(address & 0x07), value);
}

bool IEEE488Mapper::loadIntoMemory(uint8_t bank)
{
    if (!cart)
        return false;

    cart->clearCartridge(cartLocation::LO);
    cart->clearCartridge(cartLocation::HI);
    cart->clearCartridge(cartLocation::HI_E000);

    for (const auto& section : cart->getChipSections())
    {
        if (section.bankNumber != bank || section.loadAddress != 0x8000 || section.data.size() != 4096)
            continue;

        for (size_t i = 0; i < section.data.size(); ++i)
            cart->writeCartridge(static_cast<uint16_t>(i), section.data[i], cartLocation::LO);

        return true;
    }

    return false;
}

bool IEEE488Mapper::applyMappingAfterLoad()
{
    if (!cart)
        return false;

    if (!loadIntoMemory(0))
        return false;

    updateLines();

    return true;
}

void IEEE488Mapper::updateLines()
{
    if (!cart)
        return;

    cart->setGameLine(true);
    cart->setExROMLine(false);
}
