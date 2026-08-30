// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/RGCDMapper.h"

namespace
{
    constexpr bool isIO1(uint16_t address)
    {
        return address >= 0xDE00 && address <= 0xDEFF;
    }
}

RGCDMapper::RGCDMapper() :
    rgcdBank(0),
    disabled(false)
{

}

RGCDMapper::~RGCDMapper() = default;

void RGCDMapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("RGCD");
    wrtr.writeU32(1); // version
    wrtr.writeU8(rgcdBank);
    wrtr.writeBool(disabled);
    wrtr.endChunk();
}

bool RGCDMapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "RGCD", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    uint32_t ver = 0;
    if (!rdr.readU32(ver))          { rdr.exitChunkPayload(chunk); return false; }
    if (ver != 1)                   { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readU8(rgcdBank))       { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(disabled))    { rdr.exitChunkPayload(chunk); return false; }

    rgcdBank &= 0x07; // safety

    rdr.exitChunkPayload(chunk);
    return true;
}

uint8_t RGCDMapper::read(uint16_t address)
{
    (void)address;
    return cart ? cart->sampleDataBus() : 0xFF;
}

void RGCDMapper::write(uint16_t address, uint8_t value)
{
    if (!cart)
        return;

    if (!isIO1(address))
        return;

    // Once disabled, only reset may enable the cartridge again.
    if (disabled)
        return;

    if ((value & 0x08) != 0)
    {
        disabled = true;

        cart->setGameLine(true);
        cart->setExROMLine(true);
        return;
    }

    const uint8_t requestedBank =
        static_cast<uint8_t>(value & 0x07);

    if (!loadIntoMemory(requestedBank))
        return;

    // Preserve the logical register value, not the physical CHIP bank.
    rgcdBank = requestedBank;

    cart->setGameLine(true);
    cart->setExROMLine(false);
}

bool RGCDMapper::loadIntoMemory(uint8_t bank)
{
    if (!cart)
        return false;

    const uint8_t logicalBank = static_cast<uint8_t>(bank & 0x07);
    const uint8_t physicalBank = resolvePhysicalBank(logicalBank);

    for (const auto& section : cart->getChipSections())
    {
        if (section.bankNumber != physicalBank)
            continue;

        if (section.loadAddress != 0x8000)
            continue;

        if (section.data.size() != 0x2000)
            continue;

        cart->clearCartridge(cartLocation::LO);

        for (size_t i = 0; i < 0x2000; ++i)
            cart->writeCartridge(static_cast<uint16_t>(i), section.data[i], cartLocation::LO);

        return true;
    }

    std::cerr
        << "RGCD: Logical bank "
        << static_cast<unsigned>(logicalBank)
        << " resolved to physical bank "
        << static_cast<unsigned>(physicalBank)
        << ", but that CHIP bank was not found.\n";

    return false;
}

bool RGCDMapper::applyMappingAfterLoad()
{
    if (!cart)
        return false;

    if (disabled)
    {
        cart->setGameLine(true);
        cart->setExROMLine(true);
        return true;
    }

    if (!loadIntoMemory(rgcdBank))
        return false;

    cart->setGameLine(true);
    cart->setExROMLine(false);
    return true;
}

void RGCDMapper::reset()
{
    disabled = false;
    rgcdBank = 0;

    if (!cart)
        return;

    if (!loadIntoMemory(rgcdBank))
        return;

    cart->setGameLine(true);
    cart->setExROMLine(false);
}

bool RGCDMapper::isHuckyRevision() const
{
    return cart && cart->getHardwareRevision() == 1;
}

uint8_t RGCDMapper::resolvePhysicalBank(uint8_t requestedBank) const
{
    requestedBank &= 0x07;

    if (!isHuckyRevision())
        return requestedBank;

    const uint16_t count = cart->getNumberOfBanks();

    if (count == 0)
        return 0;

    const uint8_t bankCount = static_cast<uint8_t>(std::min<uint16_t>(count, 8));
    const uint8_t logicalBank = static_cast<uint8_t>(requestedBank % bankCount);

    return static_cast<uint8_t>(bankCount - 1 - logicalBank);
}

bool RGCDMapper::readDrivesBus(uint16_t address) const
{
    (void)address;
    return false;
}
