// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/SuperSnapshotV4Mapper.h"

SuperSnapshotV4Mapper::SuperSnapshotV4Mapper() :
    freezeActive(false),
    selectedBank(0xFF)
{

}

SuperSnapshotV4Mapper::~SuperSnapshotV4Mapper() = default;

void SuperSnapshotV4Mapper::SS4Control::save(StateWriter& wrtr) const
{
    wrtr.writeU8(df00);
    wrtr.writeU8(df01);
    wrtr.writeU8(df00Last);
    wrtr.writeU8(df01Last);
}

bool SuperSnapshotV4Mapper::SS4Control::load(StateReader& rdr)
{
    if (!rdr.readU8(df00))     return false;
    if (!rdr.readU8(df01))     return false;
    if (!rdr.readU8(df00Last)) return false;
    if (!rdr.readU8(df01Last)) return false;
    return true;
}

void SuperSnapshotV4Mapper::SS4Control::decodeFromDF00()
{
    const uint8_t old = df00Last;
    df00Last = df00;

    // bit2 bank
    bank = (df00 >> 2) & 0x01;

    // bit3 disable
    cartDisabled = (df00 & 0x08) != 0;

    // bit1 is a STROBE: treat as rising edge (0 -> 1)
    const bool oldB1 = (old  & 0x02) != 0;
    const bool newB1 = (df00 & 0x02) != 0;
    releaseFreeze = (!oldB1 && newB1);

    // Special rule: if bit0, bit1, bit7 are all 0 => ultimax + RAM at ROML
    const bool b0 = (df00 & 0x01) != 0;
    const bool b1 = (df00 & 0x02) != 0;
    const bool b7 = (df00 & 0x80) != 0;

    if (!b0 && !b1 && !b7)
    {
        ultimax   = true;
        ramAtROML = true;
        map16k = map8k = false;
        return;
    }

    // Otherwise: bit0 selects 16K vs 8K mapping
    ultimax   = false;
    ramAtROML = false;

    if (!b0)
    {
        map16k = true;
        map8k  = false;
    }
    else
    {
        map16k = false;
        map8k  = true;
    }
}

void SuperSnapshotV4Mapper::SS4Control::applyDF01Write(uint8_t newVal)
{
    const uint8_t old = df01Last;
    df01 = newVal;

    if (static_cast<uint8_t>(old - 1) == newVal)
    {
        // ultimax + RAM at ROML
        ultimax   = true;
        ramAtROML = true;
        map16k = map8k = false;
    }
    else if (static_cast<uint8_t>(old + 1) == newVal)
    {
        // back to ROM at ROML; restore 8K/16K decision from DF00 bit0
        ultimax   = false;
        ramAtROML = false;

        const bool b0 = (df00 & 0x01) != 0;
        map16k = !b0;
        map8k  =  b0;
    }

    df01Last = newVal;
}

void SuperSnapshotV4Mapper::SS4Control::rebuildFromSavedState()
{
    decodeFromDF00();

    // suppress any synthetic load-time strobe
    releaseFreeze = false;

    // Reconstruct DF01-derived mode state from saved df01/df01Last relation.
    const uint8_t old = df01Last;

    if (static_cast<uint8_t>(old - 1) == df01)
    {
        ultimax   = true;
        ramAtROML = true;
        map16k = map8k = false;
    }
    else if (static_cast<uint8_t>(old + 1) == df01)
    {
        ultimax   = false;
        ramAtROML = false;

        const bool b0 = (df00 & 0x01) != 0;
        map16k = !b0;
        map8k  =  b0;
    }
}

void SuperSnapshotV4Mapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("SSS4");
    wrtr.writeU32(1); // version
    ctrl.save(wrtr);
    preFreezeCtrl.save(wrtr);
    wrtr.endChunk();
}

bool SuperSnapshotV4Mapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "SSS4", 4) == 0)
    {
        rdr.enterChunkPayload(chunk);

        uint32_t ver = 0;
        if (!rdr.readU32(ver))          { rdr.exitChunkPayload(chunk); return false; }
        if (ver != 1)                   { rdr.exitChunkPayload(chunk); return false; }

        if (!ctrl.load(rdr))            { rdr.exitChunkPayload(chunk); return false; }
        if (!preFreezeCtrl.load(rdr))   { rdr.exitChunkPayload(chunk); return false; }

        // Apply immediately
        ctrl.rebuildFromSavedState();
        preFreezeCtrl.rebuildFromSavedState();

        selectedBank = 0xFF;

        if (!applyMappingAfterLoad())   { rdr.exitChunkPayload(chunk); return false; }

        rdr.exitChunkPayload(chunk);
        return true;
    }
    // Not our chunk
    return false;
}

const char* SuperSnapshotV4Mapper::getButtonName(uint32_t buttonIndex) const
{
    switch (buttonIndex)
    {
        case 0: return "Freeze";
        case 1: return "Reset";
        default: return "";
    }
}

void SuperSnapshotV4Mapper::pressButton(uint32_t buttonIndex)
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

uint8_t SuperSnapshotV4Mapper::read(uint16_t address)
{
    if ((address & 0xFF00) == 0xDE00)
    {
        size_t offset = 0x1F00 + (address & 0xFF);
        if (cart)
            return cart->hasCartridgeRAM() ? cart->readRAM(offset) : 0xFF;
        return 0xFF;
    }

    // IO2: $DF01 is readable (mirrored)
    if ((address & 0xFF00) == 0xDF00 && (address & 0x00FF) == 0x01)
        return ctrl.df01;

    // IO2 mirror: $DF02-$DFFF holds last page of first 8K of current bank
    if (address >= 0xDF02 && address <= 0xDFFF)
    {
        if (!cart) return 0xFF;

        // Mirror last 256 bytes of the first 8K of the selected bank.
        // "first 8K" corresponds to the $8000 half of the 16K bank image.
        const uint16_t idx = 0x1F00u + (address & 0x00FFu); // 0x1F00..0x1FFF

        const uint8_t bank = (selectedBank == 0xFF) ? (ctrl.bank & 0x01) : (selectedBank & 0x01);
        const auto& sections = cart->getChipSections();

        for (const auto& s : sections)
        {
            if (s.bankNumber != bank) continue;

            // 16K block at $8000
            if (s.loadAddress == 0x8000 && s.data.size() == 0x4000)
                return s.data[idx]; // idx is within 0..0x1FFF (first 8K)

            // If CRT is split into 8K chips, use the $8000 chip.
            if (s.loadAddress == 0x8000 && s.data.size() == 0x2000)
                return s.data[idx]; // idx within 0..0x1FFF for that chip
        }

        return 0xFF;
    }

    return 0xFF;
}

void SuperSnapshotV4Mapper::write(uint16_t address, uint8_t value)
{
    // IO1: $DE00-$DEFF -> last page of cart RAM ($1F00-$1FFF in an 8K RAM)
    if ((address & 0xFF00) == 0xDE00)
    {
        if (cart && cart->hasCartridgeRAM())
        {
            const size_t offset = 0x1F00 + (address & 0x00FF);
            cart->writeRAM(offset, value);
        }
        return;
    }

    // IO2: $DF00-$DFFF -> control regs + mirrors
    if ((address & 0xFF00) == 0xDF00)
    {
        const uint8_t reg = static_cast<uint8_t>(address & 0x00FF);

        if (reg == 0x00)
        {
            ctrl.df00 = value;
            ctrl.decodeFromDF00();
            (void)applyMappingAfterLoad();
            return;
        }

        if (reg == 0x01)
        {
            ctrl.applyDF01Write(value);
            (void)applyMappingAfterLoad();
            return;
        }

        return;
    }
}

bool SuperSnapshotV4Mapper::loadIntoMemory(uint8_t bank)
{
    if (!mem || !cart) return false;

    bank &= 0x01; // SSv4 has 2 banks
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

        // Most common: single 16K CHIP at $8000
        if (s.data.size() == 0x4000)
        {
            // $8000-$9FFF -> LO
            for (size_t i = 0; i < 0x2000; ++i)
                mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::LO);

            // $A000-$BFFF -> HI
            for (size_t i = 0; i < 0x2000; ++i)
                mem->writeCartridge(static_cast<uint16_t>(i), s.data[0x2000 + i], cartLocation::HI);

            // Ultimax safety mirror
            for (size_t i = 0; i < 0x2000; ++i)
                mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::HI_E000);

            break;
        }

        // Split 8K parts (handle gracefully)
        if (s.data.size() == 0x2000)
        {
            if (s.loadAddress == 0x8000)
            {
                for (size_t i = 0; i < 0x2000; ++i)
                    mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::LO);

                for (size_t i = 0; i < 0x2000; ++i)
                    mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::HI_E000);
            }
            else if (s.loadAddress == 0xA000)
            {
                for (size_t i = 0; i < 0x2000; ++i)
                    mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::HI);
            }
            else if (s.loadAddress == 0xE000)
            {
                for (size_t i = 0; i < 0x2000; ++i)
                    mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::HI_E000);
            }
            continue;
        }

        // Fallback clamp
        const size_t size = std::min(s.data.size(), static_cast<size_t>(0x2000));
        for (size_t i = 0; i < size; ++i)
            mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::LO);
        for (size_t i = 0; i < size; ++i)
            mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::HI_E000);
        break;
    }

    return any;
}

bool SuperSnapshotV4Mapper::applyMappingAfterLoad()
{
    if (!mem || !cart) return false;

    if (ctrl.releaseFreeze)
    {
        freezeActive = false;
        ctrl.releaseFreeze = false;
        ctrl = preFreezeCtrl;
        ctrl.rebuildFromSavedState();
        ctrl.releaseFreeze = false;
    }

    // If disabled, detach cart completely
    if (ctrl.cartDisabled)
    {
        cart->setExROMLine(true);
        cart->setGameLine(true);
        mem->setROMLOverlayIsRAM(false);
        return true;
    }

    // Decide which bank should be loaded
    const uint8_t wantBank = (ctrl.bank & 0x01);
    if (selectedBank != wantBank)
    {
        if (!loadIntoMemory(wantBank))
            return false;
    }

    // RAM at ROML overlay
    mem->setROMLOverlayIsRAM(ctrl.ramAtROML);

    // Mapping lines
    if (freezeActive)
    {
        cart->setExROMLine(true);
        cart->setGameLine(false);
        mem->setROMLOverlayIsRAM(true);
        return true;
    }
    else if (ctrl.map16k)
    {
        // 16K: EXROM=0, GAME=0
        cart->setExROMLine(false);
        cart->setGameLine(false);
    }
    else // ctrl.map8k
    {
        // 8K: EXROM=0, GAME=1
        cart->setExROMLine(false);
        cart->setGameLine(true);
    }

    // Consume release-freeze strobe
    ctrl.releaseFreeze = false;

    return true;
}

void SuperSnapshotV4Mapper::pressFreeze()
{
    if (!cart || !mem) return;

    if (!freezeActive)
    {
        freezeActive = true;
        preFreezeCtrl = ctrl;
    }

    // Force bank 0 in freeze mode
    (void)loadIntoMemory(0);

    // Force Ultimax mapping while frozen
    cart->setExROMLine(true);
    cart->setGameLine(false);
    mem->setROMLOverlayIsRAM(true);

    cart->requestCartridgeNMI();
}

void SuperSnapshotV4Mapper::pressReset()
{
    freezeActive = false;
    (void)applyMappingAfterLoad();

    if (!cart) return;

    cart->requestWarmReset();
}
