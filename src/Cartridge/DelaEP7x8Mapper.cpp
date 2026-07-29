// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/DelaEP7x8Mapper.h"
#include "Memory.h"

DelaEP7x8Mapper::DelaEP7x8Mapper() :
    selectedBank(0),
    disabled(false)
{

}

DelaEP7x8Mapper::~DelaEP7x8Mapper() = default;

void DelaEP7x8Mapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("E7X8");
    wrtr.writeU32(1);
    wrtr.writeU8(selectedBank);
    wrtr.writeBool(disabled);
    wrtr.endChunk();
}

bool DelaEP7x8Mapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "E7X8", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    uint32_t ver = 0;
    if (!rdr.readU32(ver))          { rdr.exitChunkPayload(chunk); return false; }
    if (ver != 1)                   { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readU8(selectedBank))  { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(disabled))    { rdr.exitChunkPayload(chunk); return false; }

    if (selectedBank > 7)           { rdr.exitChunkPayload(chunk); return false; }

    rdr.exitChunkPayload(chunk);

    return true;
}

uint8_t DelaEP7x8Mapper::read(uint16_t address)
{
    (void)address;
    return mem ? mem->getLastBus() : 0xFF;
}

void DelaEP7x8Mapper::write(uint16_t address, uint8_t value)
{
    if (!cart || !mem)
        return;

    if (address != 0xDE00)
        return;

    // All bits high disconnects the cartridge.
    if (value == 0xFF)
    {
        disabled = true;

        cart->setGameLine(true);
        cart->setExROMLine(true);
        return;
    }

    uint8_t newBank = 0;

    if (!decodeBank(value, newBank))
        return;

    if (!loadIntoMemory(newBank))
        return;

    selectedBank = newBank;
    disabled = false;

    // Normal 8K cartridge configuration.
    cart->setGameLine(true);
    cart->setExROMLine(false);
}

bool DelaEP7x8Mapper::loadIntoMemory(uint8_t bank)
{
    if (!cart || !mem)
        return false;

    if (bank > 7)
        return false;

    const Cartridge::chipSection* selectedSection = nullptr;

    for (const auto& section : cart->getChipSections())
    {
        if (section.bankNumber != bank)
            continue;

        if (section.loadAddress != CART_LO_START)
            continue;

        if (section.data.size() != 0x2000)
            continue;

        selectedSection = &section;
        break;
    }

    if (!selectedSection)
    {
        std::cerr
            << "Dela EP7x8: Bank "
            << static_cast<unsigned>(bank)
            << " not found.\n";

        return false;
    }

    cart->clearCartridge(cartLocation::LO);

    for (size_t i = 0; i < 0x2000; ++i)
        mem->writeCartridge(static_cast<uint16_t>(i), selectedSection->data[i], cartLocation::LO);

    return true;
}

bool DelaEP7x8Mapper::applyMappingAfterLoad()
{
    if (!cart || !mem)
        return false;

    if (disabled)
    {
        cart->setGameLine(true);
        cart->setExROMLine(true);
        return true;
    }

    if (!loadIntoMemory(selectedBank))
        return false;

    cart->setGameLine(true);
    cart->setExROMLine(false);

    return true;
}

bool DelaEP7x8Mapper::decodeBank(uint8_t value, uint8_t& bank) const
{
    switch (value)
    {
        case 0xFE:
            bank = 0;
            return true;
        case 0xFD:
            bank = 1;
            return true;
        case 0xFB:
            bank = 2;
            return true;
        case 0xF7:
            bank = 3;
            return true;
        case 0xEF:
            bank = 4;
            return true;
        case 0xDF:
            bank = 5;
            return true;
        case 0xBF:
            bank = 6;
            return true;
        case 0x7F:
            bank = 7;
            return true;
        default:
            return false;
    }
}

void DelaEP7x8Mapper::reset()
{
    selectedBank = 0;
    disabled = false;

    if (!cart || !mem)
        return;

    if (!loadIntoMemory(selectedBank))
        return;

    cart->setGameLine(true);
    cart->setExROMLine(false);
}
