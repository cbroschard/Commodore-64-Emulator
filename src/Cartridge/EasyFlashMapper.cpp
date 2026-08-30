// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/EasyFlashMapper.h"

EasyFlashMapper::EasyFlashMapper() :
    selectedBank(0)
{
    control.raw = 0x00;
    dfRam.fill(0x00);
}

EasyFlashMapper::~EasyFlashMapper() = default;

void EasyFlashMapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("EF00");
    wrtr.writeU32(3); // version
    wrtr.writeU8(selectedBank);
    wrtr.writeU8(control.raw);

    // Save RAM
    for (size_t i = 0; i < dfRam.size(); ++i)
        wrtr.writeU8(dfRam[i]);

    wrtr.endChunk();
}

bool EasyFlashMapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "EF00", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    uint32_t ver = 0;
    if (!rdr.readU32(ver))              { rdr.exitChunkPayload(chunk); return false; }

    if (ver != 3)                       { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readU8(selectedBank))      { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readU8(control.raw))       { rdr.exitChunkPayload(chunk); return false; }

    // Load RAM
    for (size_t i = 0; i < dfRam.size(); ++i)
        if (!rdr.readU8(dfRam[i]))      { rdr.exitChunkPayload(chunk); return false; }

    selectedBank &= 0x3F;

    // Apply immediately
    if (!applyMappingAfterLoad())   { rdr.exitChunkPayload(chunk); return false;}

    rdr.exitChunkPayload(chunk);
    return true;
}

bool EasyFlashMapper::applyMappingAfterLoad()
{
    if (!loadIntoMemory(selectedBank))
        return false;

    applyControlRegister(control.raw);
    return true;
}

uint8_t EasyFlashMapper::read(uint16_t address)
{
    if (!cart)
        return 0xFF;

    if (address >= 0xDF00 && address <= 0xDFFF)
        return dfRam[address & 0x00FF];

    // EasyFlash IO1 registers are write-only.
    if (address == 0xDE00 || address == 0xDE02)
        return cart->sampleDataBus();

    return cart->sampleDataBus();
}

void EasyFlashMapper::write(uint16_t address, uint8_t value)
{
    // EasyFlash RAM: $DF00-$DFFF
    if (address >= 0xDF00 && address <= 0xDFFF)
    {
        dfRam[address & 0x00FF] = value;
        return;
    }

    switch (address)
    {
        case 0xDE00:
        {
            // EasyFlash has 64 banks: 0-63.
            selectedBank = static_cast<uint8_t>(value & 0x3F);
            loadIntoMemory(selectedBank);
            break;
        }

        case 0xDE02:
        {
            applyControlRegister(value);
            break;
        }

        default:
            break;
    }
}

bool EasyFlashMapper::loadIntoMemory(uint8_t bank)
{
    if (!cart)
        return false;

    selectedBank = static_cast<uint8_t>(bank & 0x3F);

    cart->clearCartridge(cartLocation::LO);
    cart->clearCartridge(cartLocation::HI);
    cart->clearCartridge(cartLocation::HI_E000);

    bool loadedAny = false;

    for (const auto& sec : cart->getChipSections())
    {
        if (sec.bankNumber != selectedBank)
            continue;

        const size_t size = std::min(sec.data.size(), size_t(0x2000));

        if (sec.loadAddress == CART_LO_START || sec.loadAddress == 0x8000)
        {
            for (size_t i = 0; i < size; ++i)
                cart->writeCartridge(i, sec.data[i], cartLocation::LO);

            loadedAny = true;
        }
        else if (sec.loadAddress == CART_HI_START ||
         sec.loadAddress == 0xA000 ||
         sec.loadAddress == 0xE000)
        {
            for (size_t i = 0; i < size; ++i)
            {
                cart->writeCartridge(i, sec.data[i], cartLocation::HI);
                cart->writeCartridge(i, sec.data[i], cartLocation::HI_E000);
            }

            loadedAny = true;
        }
    }

    applyControlRegister(control.raw);

    return loadedAny;
}

void EasyFlashMapper::applyControlRegister(uint8_t value)
{
    control.set(value);

    const bool m = control.modeControl();
    const bool x = control.exromBit();
    const bool g = control.gameBit();

    if (m)
    {
        // M = 1:
        // bit 0 controls GAME directly.
        cart->setGameLine(!g);
        cart->setExROMLine(!x);
    }
    else
    {
        // M = 0:
        // GAME comes from the EasyFlash boot jumper.
        // Normal EasyFlash CRT behavior boots in Ultimax.
        cart->setGameLine(false);
        cart->setExROMLine(!x);
    }
}

void EasyFlashMapper::reset()
{
    selectedBank = 0;
    control.raw = 0x00;

    if (!cart)
        return;

    cart->setGameLine(false);
    cart->setExROMLine(true);

    loadIntoMemory(0);
}

bool EasyFlashMapper::readDrivesBus(uint16_t address) const
{
    if (!cart)
        return false;

    if (address >= 0xDF00 && address <= 0xDFFF)
        return true;

    // $DE00/$DE02 are write-only and do not drive CPU reads.
    return false;
}
