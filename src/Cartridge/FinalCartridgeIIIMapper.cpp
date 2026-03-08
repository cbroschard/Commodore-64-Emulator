// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/FinalCartridgeIIIMapper.h"

FinalCartridgeIIIMapper::FinalCartridgeIIIMapper() :
    freezeBank(0),
    freezeActive(false)
{

}

FinalCartridgeIIIMapper::~FinalCartridgeIIIMapper() = default;

void FinalCartridgeIIIMapper::FCIIIControl::decode()
{
    bank = raw & 0x03;

    const bool exromHigh = (raw & 0x10) != 0;
    const bool gameHigh  = (raw & 0x20) != 0;

    ramEnabled = false;

    releaseFreezeStrobe = false;

    cartEnabled = !(exromHigh && gameHigh);

    if (!exromHigh &&  gameHigh) mode = CartMode::cart8K;
    if (!exromHigh && !gameHigh) mode = CartMode::cart16K;
    if ( exromHigh && !gameHigh) mode = CartMode::Ultimax;
    if ( exromHigh &&  gameHigh) mode = CartMode::cart16K;
}

void FinalCartridgeIIIMapper::FCIIIControl::save(StateWriter& wrtr) const
{
    wrtr.writeU8(raw);
}

bool FinalCartridgeIIIMapper::FCIIIControl::load(StateReader& rdr)
{
    if (!rdr.readU8(raw)) { return false; }
    return true;
}

void FinalCartridgeIIIMapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("FC3I");
    wrtr.writeU32(1); //version

    ctrl.save(wrtr);
    preFreezeCtrl.save(wrtr);

    wrtr.writeU8(freezeBank);
    wrtr.writeBool(freezeActive);

    wrtr.endChunk();
}

bool FinalCartridgeIIIMapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "FC3I", 4) == 0)
    {
        rdr.enterChunkPayload(chunk);

        uint32_t ver = 0;
        if (!rdr.readU32(ver))              { rdr.exitChunkPayload(chunk); return false; }
        if (ver != 1)                       { rdr.exitChunkPayload(chunk); return false; }

        if (!ctrl.load(rdr))                { rdr.exitChunkPayload(chunk); return false; }
        ctrl.decode();

        if (!preFreezeCtrl.load(rdr))       { rdr.exitChunkPayload(chunk); return false; }
        preFreezeCtrl.decode();

        if (!rdr.readU8(freezeBank))        { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(freezeActive))    { rdr.exitChunkPayload(chunk); return false; }

        // Apply immediately
        if (!applyMappingAfterLoad())       { rdr.exitChunkPayload(chunk); return false; }

        rdr.exitChunkPayload(chunk);
        return true;
    }
    // Not our chunk
    return false;
}

const char* FinalCartridgeIIIMapper::getButtonName(uint32_t buttonIndex) const
{
    switch (buttonIndex)
    {
        case 0: return "Freeze";
        case 1: return "Reset";
        default: return "";
    }
}

void FinalCartridgeIIIMapper::pressButton(uint32_t buttonIndex)
{
    switch (buttonIndex)
    {
        case 0:
            pressFreeze();
            break;

        case 1:
            pressReset();
            break;

        default:
            break;
    }
}

uint8_t FinalCartridgeIIIMapper::read(uint16_t address)
{
    if (!cart) return 0xFF;

    if (address >= 0xDE00 && address <= 0xDFFF)
    {
        const uint8_t activeBank = freezeActive ? freezeBank : ctrl.bank;

        // Mirror last 512 bytes of the 16K bank: $3E00-$3FFF
        const uint16_t idx = 0x3E00 + (address & 0x01FF);

        const auto& sections = cart->getChipSections();
        for (const auto& s : sections)
        {
            if (s.bankNumber == activeBank &&
                s.loadAddress == 0x8000 &&
                s.data.size() == 0x4000)
            {
                return s.data[idx];
            }
        }
    }

    return 0xFF;
}

void FinalCartridgeIIIMapper::write(uint16_t address, uint8_t value)
{
    if (address != 0xDFFF)
        return;

    ctrl.raw = value;
    ctrl.decode();

    (void)applyMappingAfterLoad();

    if ((value & 0x40) == 0)
    {
        if (cart)
            cart->requestCartridgeNMI();
    }
}

bool FinalCartridgeIIIMapper::loadIntoMemory(uint8_t bank)
{
    if (!mem || !cart)
        return false;

    cart->clearCartridge(cartLocation::LO);
    cart->clearCartridge(cartLocation::HI);
    cart->clearCartridge(cartLocation::HI_E000);

    const auto& sections = cart->getChipSections();

    bool any = false;

    for (const auto& s : sections)
    {
        if (s.bankNumber != bank)
            continue;

        any = true;

        // Expected FCIII: one 16K chip at $8000
        if (s.data.size() == 0x4000)
        {
            // $8000-$9FFF -> LO
            for (size_t i = 0; i < 0x2000; ++i)
                mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::LO);

            // $A000-$BFFF -> HI
            for (size_t i = 0; i < 0x2000; ++i)
                mem->writeCartridge(static_cast<uint16_t>(i), s.data[0x2000 + i], cartLocation::HI);

            for (size_t i = 0; i < 0x2000; ++i)
                mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::HI_E000);

            break;
        }

        if (s.data.size() == 0x2000)
        {
            const uint16_t la = s.loadAddress;

            if (la == 0x8000)
            {
                for (size_t i = 0; i < 0x2000; ++i)
                    mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::LO);

                // Mirror for Ultimax safety
                for (size_t i = 0; i < 0x2000; ++i)
                    mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::HI_E000);
            }
            else if (la == 0xA000)
            {
                for (size_t i = 0; i < 0x2000; ++i)
                    mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::HI);
            }
            else if (la == 0xE000)
            {
                for (size_t i = 0; i < 0x2000; ++i)
                    mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::HI_E000);
            }

            continue;
        }

        // Fallback clamp: treat as LO + mirror
        const size_t size = std::min(s.data.size(), static_cast<size_t>(0x2000));
        for (size_t i = 0; i < size; ++i)
            mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::LO);
        for (size_t i = 0; i < size; ++i)
            mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::HI_E000);

        break;
    }

    return any;
}

bool FinalCartridgeIIIMapper::applyMappingAfterLoad()
{
    if (!cart || !mem)
        return false;

    if (freezeActive)
    {
        cart->setExROMLine(true);
        cart->setGameLine(false);
        return loadIntoMemory(freezeBank);
    }

    if (!ctrl.cartEnabled)
    {
        cart->setExROMLine(true);
        cart->setGameLine(true);
        return true;
    }

    switch (ctrl.mode)
    {
        case CartMode::cart8K:   cart->setExROMLine(false); cart->setGameLine(true);  break;
        case CartMode::cart16K:  cart->setExROMLine(false); cart->setGameLine(false); break;
        case CartMode::Ultimax:  cart->setExROMLine(true);  cart->setGameLine(false); break;
    }

    return loadIntoMemory(ctrl.bank);
}

void FinalCartridgeIIIMapper::pressFreeze()
{
    if (!cart)
        return;

    if (!freezeActive)
    {
        freezeActive = true;
        preFreezeCtrl = ctrl;
        freezeBank = 0;
        (void)loadIntoMemory(freezeBank);
    }

    cart->setExROMLine(true);
    cart->setGameLine(false);
    cart->requestCartridgeNMI();
}

void FinalCartridgeIIIMapper::pressReset()
{
    freezeActive = false;
    (void)applyMappingAfterLoad();

    if (cart)
        cart->requestWarmReset();
}
