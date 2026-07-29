// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/DelaEP256Mapper.h"
#include "Memory.h"

DelaEP256Mapper::DelaEP256Mapper() :
    selectedBank(0),
    disabled(false)
{

}

DelaEP256Mapper::~DelaEP256Mapper() = default;

void DelaEP256Mapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("E256");
    wrtr.writeU32(1);
    wrtr.writeU8(selectedBank);
    wrtr.writeBool(disabled);
    wrtr.endChunk();
}

bool DelaEP256Mapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "E256", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    uint32_t ver = 0;
    if (!rdr.readU32(ver))          { rdr.exitChunkPayload(chunk); return false; }
    if (ver != 1)                   { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readU8(selectedBank))  { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(disabled))    { rdr.exitChunkPayload(chunk); return false; }

    if (selectedBank > 32)          { rdr.exitChunkPayload(chunk); return false; }

    rdr.exitChunkPayload(chunk);

    return true;
}

uint8_t DelaEP256Mapper::read(uint16_t address)
{
    (void) address;
    return mem ? mem->getLastBus() : 0xFF;
}

void DelaEP256Mapper::write(uint16_t address, uint8_t value)
{
    if (!cart || !mem)
        return;

    if (address != 0xDE00)
        return;

    if ((value & 0x80) != 0)
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

bool DelaEP256Mapper::loadIntoMemory(uint8_t bank)
{
    if (!cart || !mem)
        return false;

    if (bank > 32)
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
            << "Dela EP256: Bank "
            << static_cast<unsigned>(bank)
            << " not found.\n";

        return false;
    }

    cart->clearCartridge(cartLocation::LO);

    for (size_t i = 0; i < 0x2000; ++i)
        mem->writeCartridge(static_cast<uint16_t>(i), selectedSection->data[i], cartLocation::LO);

    return true;
}

bool DelaEP256Mapper::applyMappingAfterLoad()
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

bool DelaEP256Mapper::decodeBank(uint8_t value, uint8_t& bank) const
{
    const uint8_t lowBits = static_cast<uint8_t>(value & 0x07);

    switch (value & 0x38)
    {
        case 0x38:
            bank = static_cast<uint8_t>(1 + lowBits);
            return true;

        case 0x28:
            bank = static_cast<uint8_t>(9 + lowBits);
            return true;

        case 0x18:
            bank = static_cast<uint8_t>(17 + lowBits);
            return true;

        case 0x08:
            bank = static_cast<uint8_t>(25 + lowBits);
            return true;

        default:
            return false;
    }
}

void DelaEP256Mapper::reset()
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
