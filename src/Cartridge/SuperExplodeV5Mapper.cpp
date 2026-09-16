// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/SuperExplodeV5Mapper.h"

SuperExplodeV5Mapper::SuperExplodeV5Mapper() :
    selectedBank(0),
    exromDisableCountdown(0),
    exromActive(true)
{

}

SuperExplodeV5Mapper::~SuperExplodeV5Mapper() = default;

void SuperExplodeV5Mapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("SPR5");
    wrtr.writeU32(2);

    wrtr.writeU8(selectedBank);
    wrtr.writeBool(exromActive);
    wrtr.writeU64(exromDisableCountdown);

    wrtr.endChunk();
}

bool SuperExplodeV5Mapper::loadState(
    const StateReader::Chunk& chunk,
    StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "SPR5", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    uint32_t ver = 0;

    if (!rdr.readU32(ver))
    {
        rdr.exitChunkPayload(chunk);
        return false;
    }

    if (ver < 1 || ver > 2)
    {
        rdr.exitChunkPayload(chunk);
        return false;
    }

    if (!rdr.readU8(selectedBank))
    {
        rdr.exitChunkPayload(chunk);
        return false;
    }

    if (ver >= 2)
    {
        if (!rdr.readBool(exromActive))
        {
            rdr.exitChunkPayload(chunk);
            return false;
        }

        if (!rdr.readU64(exromDisableCountdown))
        {
            rdr.exitChunkPayload(chunk);
            return false;
        }
    }
    else
    {
        // Old states had no RC timing state.
        exromActive = true;
        restartExromTimer();
    }

    rdr.exitChunkPayload(chunk);

    return true;
}

void SuperExplodeV5Mapper::reset()
{
    selectedBank = 0;
    exromActive = true;

    restartExromTimer();

    (void)applyMappingAfterLoad();
}

void SuperExplodeV5Mapper::tick(uint32_t cycles)
{
    if (!exromActive)
        return;

    if (exromDisableCountdown > cycles)
    {
        exromDisableCountdown -= cycles;
        return;
    }

    exromDisableCountdown = 0;
    exromActive = false;

    updateLines();
}

uint8_t SuperExplodeV5Mapper::read(uint16_t address)
{
    if (!cart)
        return 0xFF;

    if (address >= 0x8000 && address <= 0x9FFF)
    {
        refreshExrom();
        return cart->readCartridge(static_cast<uint16_t>(address - 0x8000), cartLocation::LO);
    }

    if (address >= 0xDF00 && address <= 0xDFFF)
    {
        refreshExrom();
        const uint16_t romOffset = static_cast<uint16_t>(0x1F00 + (address & 0x00FF));
        return cart->readCartridge(romOffset, cartLocation::LO);
    }

    return cart->sampleDataBus();
}

void SuperExplodeV5Mapper::write(uint16_t address, uint8_t value)
{
    if (!cart)
        return;

    if (address >= 0x8000 && address <= 0x9FFF)
    {
        refreshExrom();
        return;
    }

    if (address >= 0xDF00 && address <= 0xDFFF)
    {
        refreshExrom();

        if (address == 0xDF00)
        {
            selectedBank = (value & 0x80) ? 1 : 0;
            (void)loadIntoMemory(selectedBank);
        }
    }
}

bool SuperExplodeV5Mapper::loadIntoMemory(uint8_t bank)
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

        if (section.data.size() != 8192)
            continue;

        for (size_t i = 0; i < 8192; ++i)
            cart->writeCartridge(static_cast<uint16_t>(i), section.data[i], cartLocation::LO);

        return true;
    }


    return false;
}

bool SuperExplodeV5Mapper::applyMappingAfterLoad()
{
    if (!cart)
        return false;

    selectedBank &= 0x01;

    updateLines();

    return loadIntoMemory(selectedBank);
}

bool SuperExplodeV5Mapper::romReadHandledByMapper(uint16_t address) const
{
    return address >= 0x8000 && address <= 0x9FFF;
}

bool SuperExplodeV5Mapper::romWriteEnabled(uint16_t address) const
{
    return address >= 0x8000 && address <= 0x9FFF;
}

void SuperExplodeV5Mapper::updateLines()
{
    if (!cart)
        return;

    cart->setGameLine(true);

    // exromActive=true means /EXROM asserted low.
    cart->setExROMLine(!exromActive);
}

void SuperExplodeV5Mapper::restartExromTimer()
{
    if (!cart)
    {
        exromDisableCountdown = 0;
        return;
    }

    const double clockHz = cart->getCPUClockHz();

    if (clockHz <= 0.0)
    {
        exromDisableCountdown = 0;
        return;
    }

    // Approximately 300 ms.
    exromDisableCountdown = static_cast<uint64_t>(clockHz * 0.300);
}

void SuperExplodeV5Mapper::refreshExrom()
{
    const bool wasInactive = !exromActive;

    exromActive = true;
    restartExromTimer();

    if (wasInactive)
        updateLines();
}
