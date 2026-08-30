// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/DelaEP64Mapper.h"

DelaEP64Mapper::DelaEP64Mapper() :
    selectedBank(0),
    disabled(false)
{

}

DelaEP64Mapper::~DelaEP64Mapper() = default;

void DelaEP64Mapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("EP64");
    wrtr.writeU32(1); // version
    wrtr.writeU8(selectedBank);
    wrtr.writeBool(disabled);
    wrtr.endChunk();
}

bool DelaEP64Mapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
   if (std::memcmp(chunk.tag, "EP64", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    uint32_t ver = 0;
    if (!rdr.readU32(ver))          { rdr.exitChunkPayload(chunk); return false; }
    if (ver != 1)                   { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readU8(selectedBank))  { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(disabled))    { rdr.exitChunkPayload(chunk); return false; }

    rdr.exitChunkPayload(chunk);

    return true;
}

uint8_t DelaEP64Mapper::read(uint16_t address)
{
    (void)address;
    return cart ? cart->sampleDataBus() : 0xFF;
}

void DelaEP64Mapper::write(uint16_t address, uint8_t value)
{
    if (!cart)
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

    const uint8_t newBank = decodeBank(value);

    if (!loadIntoMemory(newBank))
        return;

    selectedBank = newBank;
    disabled = false;

    cart->setGameLine(true);
    cart->setExROMLine(false);
}

bool DelaEP64Mapper::loadIntoMemory(uint8_t bank)
{
    if (!cart)
        return false;

    const uint8_t requestedBank = static_cast<uint8_t>(std::min<uint8_t>(bank, 8));

    const uint8_t* source = nullptr;

    for (const auto& section : cart->getChipSections())
    {
        if (section.loadAddress != 0x8000)
            continue;

        // One separately stored 8K logical bank.
        if (section.data.size() == 0x2000 && section.bankNumber == requestedBank)
        {
            source = section.data.data();
            break;
        }

        // First 32K EPROM packet contains logical banks 1-4.
        if (section.data.size() == 0x8000 && section.bankNumber == 1 && requestedBank >= 1 && requestedBank <= 4)
        {
            const size_t offset = static_cast<size_t>(requestedBank - 1) *
                0x2000;

            source = section.data.data() + offset;
            break;
        }

        // Second 32K EPROM packet contains logical banks 5-8.
        if (section.data.size() == 0x8000 && section.bankNumber == 2 && requestedBank >= 5 && requestedBank <= 8)
        {
            const size_t offset = static_cast<size_t>(requestedBank - 5) * 0x2000;

            source = section.data.data() + offset;
            break;
        }
    }

    if (!source)
    {
        std::cerr
            << "Dela EP64: Bank "
            << static_cast<unsigned>(requestedBank)
            << " not found.\n";

        return false;
    }

    cart->clearCartridge(cartLocation::LO);

    for (size_t i = 0; i < 0x2000; ++i)
        cart->writeCartridge(static_cast<uint16_t>(i), source[i], cartLocation::LO);

    return true;
}

bool DelaEP64Mapper::applyMappingAfterLoad()
{
    if (!cart)
        return false;

    if (disabled)
    {
        // Cartridge removed from the memory map.
        cart->setGameLine(true);
        cart->setExROMLine(true);
        return true;
    }

    if (!loadIntoMemory(selectedBank))
        return false;

    // Normal 8K cartridge mode.
    cart->setGameLine(true);
    cart->setExROMLine(false);

    return true;
}

uint8_t DelaEP64Mapper::decodeBank(uint8_t value) const
{
    const uint8_t socketSelect = static_cast<uint8_t>(value & 0x03);

    const uint8_t epromBank = static_cast<uint8_t>((value >> 4) & 0x03);

    if (socketSelect == 0x01)
        return static_cast<uint8_t>(1 + epromBank);

    if (socketSelect == 0x02)
        return static_cast<uint8_t>(5 + epromBank);

    return 0;
}

void DelaEP64Mapper::reset()
{
    selectedBank = 0;
    disabled = false;

    if (!cart)
        return;

    if (!loadIntoMemory(selectedBank))
        return;

    cart->setGameLine(true);
    cart->setExROMLine(false);
}

bool DelaEP64Mapper::readDrivesBus(uint16_t address) const
{
    (void)address;
    return false;
}
