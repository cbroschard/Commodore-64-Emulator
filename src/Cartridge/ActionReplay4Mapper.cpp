// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/ActionReplay4Mapper.h"

ActionReplay4Mapper::ActionReplay4Mapper() :
    selectedBank(0),
    freezeActive(false),
    preFreezeSelectedBank(0)
{

}

ActionReplay4Mapper::~ActionReplay4Mapper() = default;

void ActionReplay4Mapper::AR4Control::save(StateWriter& wrtr) const
{
    wrtr.writeU8(raw);
}

bool ActionReplay4Mapper::AR4Control::load(StateReader& rdr)
{
    if (!rdr.readU8(raw))    return false;
    decode();

    return true;
}

void ActionReplay4Mapper::AR4Control::decode()
{
    // bit 0 = bank bit 0
    // bit 4 = bank bit 1
    romBank = ((raw >> 0) & 0x01) |(((raw >> 4) & 0x01) << 1);

    // bit 1: 0 sets GAME low, 1 sets GAME high
    gameLow = ((raw & 0x02) == 0);

    // bit 3: 1 sets EXROM low, 0 sets EXROM high
    exromLow = ((raw & 0x08) != 0);

    // bit 2: freeze-end / disable register / hide ROM
    cartDisabled = ((raw & 0x04) != 0);
}

void ActionReplay4Mapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("A4RP");
    wrtr.writeU32(1); // version

    ctrl.save(wrtr);
    preFreezeCtrl.save(wrtr);

    wrtr.writeU8(selectedBank);
    wrtr.writeU8(preFreezeSelectedBank);
    wrtr.writeBool(freezeActive);

    wrtr.endChunk();
}

bool ActionReplay4Mapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "A4RP", 4) == 0)
    {
        uint32_t ver = 0;
        if (!rdr.readU32(ver))                  { rdr.exitChunkPayload(chunk); return false; }
        if (ver != 1)                           { rdr.exitChunkPayload(chunk); return false; }

        if (!ctrl.load(rdr))                    { rdr.exitChunkPayload(chunk); return false; }
        if (!preFreezeCtrl.load(rdr))           { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readU8(selectedBank))          { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU8(preFreezeSelectedBank)) { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(freezeActive))        { rdr.exitChunkPayload(chunk); return false; }

        if (!applyMappingAfterLoad())           { rdr.exitChunkPayload(chunk); return false; }

        rdr.exitChunkPayload(chunk);
        return true;
    }

    // Not our chunk
    return false;
}

uint8_t ActionReplay4Mapper::read(uint16_t address)
{
    if (!mem || !cart)
        return 0xFF;

    // If the cart is disabled, IO reads should behave like no cart there.
    if (ctrl.cartDisabled)
        return 0xFF;

    // IO2 ($DF00-$DFFF): first page of the currently banked ROM block.
    if ((address & 0xFF00) == 0xDF00)
    {
        return mem->readCartridge(static_cast<uint16_t>(address & 0x00FF), cartLocation::LO);
    }

    return 0xFF;
}

void ActionReplay4Mapper::write(uint16_t address, uint8_t value)
{
    if ((address & 0xFF00) != 0xDE00)
        return;

    if (ctrl.cartDisabled)
        return;

    ctrl.raw = value;
    ctrl.decode();

    freezeActive = false;

    applyMappingFromControl();
}

bool ActionReplay4Mapper::loadIntoMemory(uint8_t bank)
{
    if (!mem || !cart) return false;

    selectedBank = bank;

    cart->clearCartridge(cartLocation::LO);
    cart->clearCartridge(cartLocation::HI);
    cart->clearCartridge(cartLocation::HI_E000);

    const auto& sections = cart->getChipSections();

    bool any = false;
    for (const auto& s : sections)
    {
        if (s.bankNumber != bank) continue;
        any = true;

        // If a section is 16K, split it LO/HI like you already do
        if (s.data.size() == 0x4000)
        {
            // $8000-$9FFF
            for (size_t i = 0; i < 0x2000; ++i)
                mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::LO);

            // $A000-$BFFF
            for (size_t i = 0x2000; i < 0x4000; ++i)
                mem->writeCartridge(static_cast<uint16_t>(i - 0x2000), s.data[i], cartLocation::HI);

            // Action Replay Ultimax/freeze needs ROM at $E000-$FFFF for vectors.
            for (size_t i = 0; i < 0x2000; ++i)
                mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::HI_E000);

            continue;
        }

        // If a section is 8K, route it based on loadAddress
        if (s.data.size() == 0x2000)
        {
            const uint16_t la = s.loadAddress;

            if (la == 0x8000)
            {
                // ROML ($8000-$9FFF)
                for (size_t i = 0; i < 0x2000; ++i)
                    mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::LO);

                for (size_t i = 0; i < 0x2000; ++i)
                    mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::HI_E000);

                continue;
            }
            else if (la == 0xA000)
            {
                // ROMH ($A000-$BFFF) in 16K mode
                for (size_t i = 0; i < 0x2000; ++i)
                    mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::HI);

                continue;
            }
            else if (la == 0xE000)
            {
                // Explicit ROM at $E000-$FFFF
                for (size_t i = 0; i < 0x2000; ++i)
                    mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::HI_E000);

                continue;
            }
            else
            {
                // Fallback: treat as ROML
                for (size_t i = 0; i < 0x2000; ++i)
                    mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::LO);

                // Mirror to $E000 as well, so Ultimax won't explode
                for (size_t i = 0; i < 0x2000; ++i)
                    mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::HI_E000);

                continue;
            }
        }

        // Fallback: clamp and load as LO (and mirror to HI_E000 for safety)
        {
            const size_t size = std::min(s.data.size(), static_cast<size_t>(0x2000));

            for (size_t i = 0; i < size; ++i)
                mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::LO);

            for (size_t i = 0; i < size; ++i)
                mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::HI_E000);
        }
    }

    return any;
}

const char* ActionReplay4Mapper::getButtonName(uint32_t buttonIndex) const
{
    switch(buttonIndex)
    {
        case 0:  return "freeze";
        case 1:  return "reset";
        default: return "";
    }
}

bool ActionReplay4Mapper::applyMappingAfterLoad()
{
    if (!mem || !cart)
        return false;

    applyMappingFromControl();
    return true;
}

void ActionReplay4Mapper::applyMappingFromControl()
{
    if (!cart || !mem)
        return;

    if (ctrl.cartDisabled)
    {
        cart->clearCartridge(cartLocation::LO);
        cart->clearCartridge(cartLocation::HI);
        cart->clearCartridge(cartLocation::HI_E000);

        cart->setGameLine(true);
        cart->setExROMLine(true);
        return;
    }

    loadIntoMemory(ctrl.romBank);

    cart->setGameLine(ctrl.gameLow);
    cart->setExROMLine(ctrl.exromLow);
}

void ActionReplay4Mapper::pressButton(uint32_t buttonIndex)
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

void ActionReplay4Mapper::pressFreeze()
{
    if (!cart || !mem)
        return;

    if (!freezeActive)
    {
        preFreezeCtrl = ctrl;
        preFreezeSelectedBank = selectedBank;
    }
    freezeActive = true;

    ctrl.raw = 0x03;
    ctrl.decode();

    applyMappingFromControl();

    cart->requestCartridgeNMI();
}

void ActionReplay4Mapper::pressReset()
{
    freezeActive = false;
    preFreezeSelectedBank = 0;

    ctrl.raw = 0x00;
    ctrl.decode();

    preFreezeCtrl.raw = 0;
    preFreezeCtrl.decode();

    applyMappingFromControl();

    if (cart)
        cart->requestWarmReset();
}
