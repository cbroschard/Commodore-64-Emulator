// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include <algorithm>
#include <array>
#include "Cartridge.h"
#include "Cartridge/ProtovisionMegabyterMapper.h"

ProtovisionMegabyterMapper::ProtovisionMegabyterMapper() :
    bankRegister(0x00),
    modeRegister(0x00)
{

}

ProtovisionMegabyterMapper::~ProtovisionMegabyterMapper() = default;

void ProtovisionMegabyterMapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("PMBT");
    wrtr.writeU32(1); // Version

    wrtr.writeU8(bankRegister);
    wrtr.writeU8(modeRegister);

    flash.savePayload(wrtr);

    wrtr.endChunk();
}

bool ProtovisionMegabyterMapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "PMBT", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    uint32_t ver = 0;
    if (!rdr.readU32(ver))       { rdr.exitChunkPayload(chunk); return false; }
    if (ver != 1)                { rdr.exitChunkPayload(chunk); return false; }

    uint8_t newBank = 0;
    uint8_t newMode = 0;

    if (!rdr.readU8(newBank))    { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readU8(newMode))    { rdr.exitChunkPayload(chunk); return false; }

    if (!flash.loadPayload(rdr)) { rdr.exitChunkPayload(chunk); return false; }

    bankRegister = newBank & 0x7F;
    modeRegister = newMode & 0x83;

    rdr.exitChunkPayload(chunk);
    return true;
}

void ProtovisionMegabyterMapper::reset()
{
    bankRegister = 0x00;
    modeRegister = 0x00;

    flash.reset();
    updateLines();
}

void ProtovisionMegabyterMapper::tick(uint32_t elapsedCycles)
{
    flash.tick(elapsedCycles);
}

uint8_t ProtovisionMegabyterMapper::read(uint16_t address)
{
    if (!cart)
        return 0xFF;

    if (address >= 0x8000 && address <= 0x9FFF)
        return flash.read(translateFlashAddress(address));

    return cart->sampleDataBus();
}

void ProtovisionMegabyterMapper::write(uint16_t address, uint8_t value)
{
    if (!cart)
        return;

    if (address >= 0x8000 && address <= 0x9FFF)
    {
        flash.write(translateFlashAddress(address), value);
        return;
    }

    if (address >= 0xDE00 && address <= 0xDEFF)
    {
        if (address & 0x02)
        {
            // Memory configuration and LED.
            modeRegister = value & 0x83;
            updateLines();
        }
        else
        {
            // Select one of 128 flash banks.
            bankRegister = value & 0x7F;
        }

        return;
    }
}

bool ProtovisionMegabyterMapper::romReadHandledByMapper(uint16_t address) const
{
    return address >= 0x8000 && address <= 0x9FFF;
}

bool ProtovisionMegabyterMapper::romWriteEnabled(uint16_t address) const
{
    return address >= 0x8000 && address <= 0x9FFF;
}

bool ProtovisionMegabyterMapper::readDrivesBus(uint16_t address) const
{
    return address >= 0x8000 && address <= 0x9FFF;
}

bool ProtovisionMegabyterMapper::loadIntoMemory(uint8_t bank)
{
    if (!cart)
        return false;

    auto image = std::make_unique<std::array<uint8_t, AMD29F800Flash::FLASH_SIZE>>();

    image->fill(0xFF);

    bool found = false;

    for (const auto& section : cart->getChipSections())
    {
        if (section.bankNumber >= 128 || section.loadAddress != 0x8000 || section.data.size() != 0x2000)
            continue;

        const size_t offset = static_cast<size_t>(section.bankNumber) * 0x2000;

        std::copy(section.data.begin(), section.data.end(), image->begin() + offset);

        found = true;
    }

    if (!found)
        return false;

    flash.loadImage(image->data(), image->size());

    bankRegister = bank & 0x7F;
    modeRegister = 0x00;

    updateLines();
    return true;
}

bool ProtovisionMegabyterMapper::applyMappingAfterLoad()
{
    if (!cart)
        return false;

    updateLines();
    return true;
}

uint32_t ProtovisionMegabyterMapper::translateFlashAddress(uint16_t address) const
{
    return (static_cast<uint32_t>(bankRegister & 0x7F) << 13) | (address & 0x1FFF);
}

void ProtovisionMegabyterMapper::updateLines()
{
    if (!cart)
        return;

    cart->setGameLine((modeRegister & 0x01) == 0);
    cart->setExROMLine((modeRegister & 0x02) != 0);
}
