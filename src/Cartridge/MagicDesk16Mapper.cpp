// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/MagicDesk16Mapper.h"
#include "Memory.h"

namespace
{
    constexpr bool isIO1(uint16_t address)
    {
        return address >= 0xDE00 &&
               address <= 0xDEFF;
    }
}

MagicDesk16Mapper::MagicDesk16Mapper() :
    magicDeskBank(0),
    disabled(false)
{

}

MagicDesk16Mapper::~MagicDesk16Mapper() = default;

void MagicDesk16Mapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("MD16");
    wrtr.writeU32(1); // version
    wrtr.writeU8(magicDeskBank);
    wrtr.writeBool(disabled);
    wrtr.endChunk();
}

bool MagicDesk16Mapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "MD16", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    uint32_t ver = 0;
    if (!rdr.readU32(ver))          { rdr.exitChunkPayload(chunk); return false; }
    if (ver != 1)                   { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readU8(magicDeskBank)) { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(disabled))    { rdr.exitChunkPayload(chunk); return false; }

    magicDeskBank &= 0x7F; // safety

    rdr.exitChunkPayload(chunk);
    return true;
}

uint8_t MagicDesk16Mapper::read(uint16_t address)
{
    (void)address;

    return cart ? cart->sampleDataBus() : 0xFF;

    return 0xFF;
}

void MagicDesk16Mapper::write(uint16_t address, uint8_t value)
{
    if (!cart || !mem)
        return;

    if (!isIO1(address))
        return;

    const bool newDisabled =
        (value & 0x80) != 0;

    const uint8_t newBank =
        static_cast<uint8_t>(value & 0x7F);

    if (newDisabled)
    {
        disabled = true;

        cart->setGameLine(true);
        cart->setExROMLine(true);
        return;
    }

    if (!loadIntoMemory(newBank))
        return;

    magicDeskBank = newBank;
    disabled = false;

    cart->setGameLine(false);
    cart->setExROMLine(false);
}

bool MagicDesk16Mapper::loadIntoMemory(uint8_t bank)
{
    if (!cart || !mem)
        return false;

    const uint8_t selectedBank = static_cast<uint8_t>(bank & 0x7F);

    const Cartridge::chipSection* selectedSection = nullptr;

    for (const auto& section : cart->getChipSections())
    {
        if (section.bankNumber != selectedBank)
            continue;

        if (section.loadAddress != 0x8000)
            continue;

        if (section.data.size() != 0x4000)
            continue;

        selectedSection = &section;
        break;
    }

    if (!selectedSection)
    {
        std::cerr
            << "Magic Desk 16: Bank "
            << static_cast<unsigned>(selectedBank)
            << " not found.\n";

        return false;
    }

    cart->clearCartridge(cartLocation::LO);
    cart->clearCartridge(cartLocation::HI);

    for (size_t i = 0; i < 0x2000; ++i)
    {
        mem->writeCartridge(static_cast<uint16_t>(i), selectedSection->data[i], cartLocation::LO);

        mem->writeCartridge(static_cast<uint16_t>(i), selectedSection->data[i + 0x2000], cartLocation::HI);
    }

    return true;
}

bool MagicDesk16Mapper::applyMappingAfterLoad()
{
    if (!cart || !mem)
        return false;

    magicDeskBank &= 0x7F;

    if (disabled)
    {
        cart->setGameLine(true);
        cart->setExROMLine(true);
        return true;
    }

    if (!loadIntoMemory(magicDeskBank))
        return false;

    cart->setGameLine(false);
    cart->setExROMLine(false);
    return true;
}

void MagicDesk16Mapper::reset()
{
    magicDeskBank = 0;
    disabled = false;

    if (!cart || !mem)
        return;

    if (!loadIntoMemory(magicDeskBank))
        return;

    cart->setGameLine(false);
    cart->setExROMLine(false);
}
