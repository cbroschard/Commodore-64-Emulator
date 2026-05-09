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
    control.raw = 0x05; // M=1, X=0, G=1 => /GAME low, /EXROM high, Ultimax boot
    dfRam.fill(0x00);
}

EasyFlashMapper::~EasyFlashMapper() = default;

void EasyFlashMapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("EF00");
    wrtr.writeU32(2); // version
    wrtr.writeU8(selectedBank);
    wrtr.writeU8(control.raw);
    wrtr.endChunk();
}

bool EasyFlashMapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "EF00", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    uint32_t ver = 0;
    if (!rdr.readU32(ver)) { rdr.exitChunkPayload(chunk); return false; }

    if (ver == 1)
    {
        if (!rdr.readU8(selectedBank)) { rdr.exitChunkPayload(chunk); return false; }
        control.raw = 0x05;
    }
    else if (ver == 2)
    {
        if (!rdr.readU8(selectedBank)) { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU8(control.raw))  { rdr.exitChunkPayload(chunk); return false; }
    }
    else
    {
        rdr.exitChunkPayload(chunk);
        return false;
    }

    selectedBank &= 0x3F;

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
    // EasyFlash RAM: $DF00-$DFFF, 256 bytes
    if (address >= 0xDF00 && address <= 0xDFFF)
        return dfRam[address & 0x00FF];

    // EasyFlash control/status area
    if (address == 0xDE02)
    {
        uint8_t value = 0xFF;

        if (cart->getGameLine())
            value |= 0x01;
        else
            value &= ~0x01;

        if (cart->getExROMLine())
            value |= 0x02;
        else
            value &= ~0x02;

        // MODE bit: EasyFlash mode = 0
        value &= ~0x04;

        return value;
    }

    return 0xFF;
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
    if (!mem || !cart)
        return false;

    selectedBank = static_cast<uint8_t>(bank & 0x3F);

    cart->clearCartridge(cartLocation::LO);
    cart->clearCartridge(cartLocation::HI);

    bool loadedAny = false;

    for (const auto& sec : cart->getChipSections())
    {
        if (sec.bankNumber != selectedBank)
            continue;

        const size_t size = std::min(sec.data.size(), size_t(0x2000));

        if (sec.loadAddress == CART_LO_START || sec.loadAddress == 0x8000)
        {
            for (size_t i = 0; i < size; ++i)
                mem->writeCartridge(i, sec.data[i], cartLocation::LO);

            loadedAny = true;
        }
        else if (sec.loadAddress == CART_HI_START ||
         sec.loadAddress == 0xA000 ||
         sec.loadAddress == 0xE000)
        {
            for (size_t i = 0; i < size; ++i)
            {
                mem->writeCartridge(i, sec.data[i], cartLocation::HI);
                mem->writeCartridge(i, sec.data[i], cartLocation::HI_E000);
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
        cart->setGameLine(!g);
        cart->setExROMLine(!x);
    }
    else
    {
        cart->setExROMLine(!x);
    }
}
