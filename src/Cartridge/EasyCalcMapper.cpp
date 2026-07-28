// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/EasyCalcMapper.h"
#include "Memory.h"

EasyCalcMapper::EasyCalcMapper() :
    easyCalcBank(0)
{

}

EasyCalcMapper::~EasyCalcMapper() = default;

void EasyCalcMapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("ECLC");
    wrtr.writeU32(1); // version
    wrtr.writeU8(easyCalcBank);
    wrtr.endChunk();
}

bool EasyCalcMapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "ECLC", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    uint32_t ver = 0;
    if (!rdr.readU32(ver))          { rdr.exitChunkPayload(chunk); return false; }
    if (ver != 1)                   { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readU8(easyCalcBank))  { rdr.exitChunkPayload(chunk); return false; }

    easyCalcBank &= 0x01;

    rdr.exitChunkPayload(chunk);
    return true;
}

uint8_t EasyCalcMapper::read(uint16_t address)
{
    (void)address;

    return mem ? mem->getLastBus() : 0xFF;
}

void EasyCalcMapper::write(uint16_t address, uint8_t value)
{
    (void)value;

    if (!cart || !mem)
        return;

    if (address < 0xDE00 || address > 0xDEFF)
        return;

    const uint8_t newBank =
        static_cast<uint8_t>(address & 0x01);

    if (!loadIntoMemory(newBank))
        return;

    easyCalcBank = newBank;
}

bool EasyCalcMapper::loadIntoMemory(uint8_t bank)
{
    if (!cart || !mem)
        return false;

    const uint8_t selectedBank = static_cast<uint8_t>(bank & 0x01);

    const Cartridge::chipSection* loSection = nullptr;
    const Cartridge::chipSection* hiSection = nullptr;

    for (const auto& section : cart->getChipSections())
    {
        if (section.data.size() != 0x2000)
            continue;

        if (section.bankNumber == 0 && section.loadAddress == 0x8000)
            loSection = &section;

        if (section.bankNumber == selectedBank &&
            section.loadAddress == 0xA000)
        {
            hiSection = &section;
        }
    }

    if (!loSection || !hiSection)
    {
        std::cerr
            << "EasyCalc: Required ROM section missing for HI bank "
            << static_cast<unsigned>(selectedBank)
            << ".\n";

        return false;
    }

    cart->clearCartridge(cartLocation::LO);
    cart->clearCartridge(cartLocation::HI);

    for (size_t i = 0; i < 0x2000; ++i)
    {
        mem->writeCartridge(static_cast<uint16_t>(i), loSection->data[i], cartLocation::LO);

        mem->writeCartridge(static_cast<uint16_t>(i), hiSection->data[i], cartLocation::HI);
    }

    return true;
}

void EasyCalcMapper::reset()
{
    easyCalcBank = 0;

    if (!cart || !mem)
        return;

    if (!loadIntoMemory(easyCalcBank))
        return;

    cart->setGameLine(false);
    cart->setExROMLine(false);
}

bool EasyCalcMapper::applyMappingAfterLoad()
{
    if (!cart || !mem)
        return false;

    easyCalcBank &= 0x01;

    if (!loadIntoMemory(easyCalcBank))
        return false;

    // EasyCalc always operates as a 16K game cartridge.
    cart->setGameLine(false);
    cart->setExROMLine(false);

    return true;
}
