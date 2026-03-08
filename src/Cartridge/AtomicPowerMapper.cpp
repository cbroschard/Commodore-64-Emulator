// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/AtomicPowerMapper.h"

AtomicPowerMapper::AtomicPowerMapper() :
    freezeActive(false),
    preFreezeRaw(0),
    preFreezeBank(0),
    selectedBank(0xFF),
    loadedBank(0xFF),
    ramEnabled(false)
{

}

AtomicPowerMapper::~AtomicPowerMapper() = default;

void AtomicPowerMapper::APControl::save(StateWriter& wrtr) const
{
    wrtr.writeU8(raw);
}

bool AtomicPowerMapper::APControl::load(StateReader& rdr)
{
    if (!rdr.readU8(raw))   return false;
    return true;
}

void AtomicPowerMapper::APControl::decode()
{
    // Bank select: A13/A14 from bits 3/4 -> 0..3
    bank = (raw >> 3) & 0x03;

    ramEnable   = (raw & (1 << 5)) != 0;
    cartDisable = (raw & (1 << 2)) != 0;
    freezeClear = (raw & (1 << 6)) != 0;
    exromHigh   = (raw & (1 << 1)) != 0;
    gameLow     = (raw & (1 << 0)) != 0;
}

void AtomicPowerMapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("ATPW");
    wrtr.writeU32(1);

    ctrl.save(wrtr);

    wrtr.writeU8(selectedBank);
    wrtr.writeBool(ramEnabled);

    wrtr.writeBool(freezeActive);
    wrtr.writeU8(preFreezeRaw);
    wrtr.writeU8(preFreezeBank);

    wrtr.endChunk();
}

bool AtomicPowerMapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "ATPW", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    uint32_t ver = 0;
    if (!rdr.readU32(ver))              { rdr.exitChunkPayload(chunk); return false; }
    if (ver != 1)                       { rdr.exitChunkPayload(chunk); return false; }

    if (!ctrl.load(rdr))                { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readU8(selectedBank))      { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(ramEnabled))      { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readBool(freezeActive))    { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readU8(preFreezeRaw))      { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readU8(preFreezeBank))     { rdr.exitChunkPayload(chunk); return false; }

    rdr.exitChunkPayload(chunk);

    ctrl.decode();
    loadedBank = 0xFF; // force a reload
    if (!applyMappingAfterLoad()) return false;
    return true;
}

const char* AtomicPowerMapper::getButtonName(uint32_t buttonIndex) const
{
    switch (buttonIndex)
    {
        case 0: return "Freeze";
        case 1: return "Reset";
        default: return "";
    }
}

void AtomicPowerMapper::pressButton(uint32_t buttonIndex)
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

uint8_t AtomicPowerMapper::read(uint16_t address)
{
    if (!cart || !mem) return 0xFF;

    // IO2 window: DF00-DFFF -> window into $9F00-$9FFF of currently selected thing
    if (address >= 0xDF00 && address <= 0xDFFF)
    {
        const uint16_t offset = 0x1F00 | (address & 0x00FF);

        if (ramEnabled)
        {
            return cart->readRAM(offset);
        }
        else
        {
            return mem->readCartridge(offset, cartLocation::LO);
        }
    }

    return 0xFF;
}

void AtomicPowerMapper::write(uint16_t address, uint8_t value)
{
    if (!cart || !mem) return;

    // IO1: $DE00-$DEFF -> control register
    if ((address & 0xFF00) == 0xDE00)
    {
        ctrl.raw = value;
        ctrl.decode();

        // Freeze-clear is a momentary action: if asserted while frozen, restore pre-freeze control.
        if (ctrl.freezeClear && freezeActive)
        {
            freezeActive = false;

            ctrl.raw = preFreezeRaw;
            ctrl.decode();
        }

        applyMappingAfterLoad();
        return;
    }

    // IO2: $DF00-$DFFF -> window into $9F00-$9FFF (tail of ROML/RAM)
    if ((address & 0xFF00) == 0xDF00)
    {
        if (!ramEnabled)
            return;

        const uint16_t offset = 0x1F00 | (address & 0x00FF);
        cart->writeRAM(offset, value);
        return;
    }
}

bool AtomicPowerMapper::loadIntoMemory(uint8_t bank)
{
    if (!mem || !cart) return false;

    bank &= 0x03; // Atomic Power has 4 banks (32KiB / 8KiB)
    selectedBank = bank;

    // Atomic Power only uses ROML ($8000-$9FFF).
    // Clear LO and any safety mirror regions we may use.
    cart->clearCartridge(cartLocation::LO);
    cart->clearCartridge(cartLocation::HI);
    cart->clearCartridge(cartLocation::HI_E000);

    const auto& sections = cart->getChipSections();

    bool any = false;

    // Typical CRT layout for Atomic Power: 4 CHIP sections, each:
    //   bankNumber = 0..3
    //   loadAddress = 0x8000
    //   size = 0x2000 (8KiB)
    for (const auto& s : sections)
    {
        if (s.bankNumber != bank) continue;

        // We only care about the 8K module at $8000
        if (s.loadAddress != 0x8000)
            continue;

        if (s.data.size() < 0x2000)
            continue;

        any = true;

        // $8000-$9FFF -> LO (offset 0..0x1FFF)
        for (size_t i = 0; i < 0x2000; ++i)
            mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::LO);

        // Safety mirror for Ultimax-ish cases (harmless if never used)
        for (size_t i = 0; i < 0x2000; ++i)
            mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::HI_E000);

        break;
    }

    if (!any)
    {
        for (const auto& s : sections)
        {
            if (s.loadAddress != 0x8000)
                continue;

            if (s.data.size() == 0x8000) // 32KiB in one CHIP
            {
                const size_t base = static_cast<size_t>(bank) * 0x2000;
                if (base + 0x2000 > s.data.size())
                    break;

                any = true;

                for (size_t i = 0; i < 0x2000; ++i)
                    mem->writeCartridge(static_cast<uint16_t>(i), s.data[base + i], cartLocation::LO);

                for (size_t i = 0; i < 0x2000; ++i)
                    mem->writeCartridge(static_cast<uint16_t>(i), s.data[base + i], cartLocation::HI_E000);

                break;
            }
        }
    }

    return any;
}

bool AtomicPowerMapper::applyMappingAfterLoad()
{
    if (!cart || !mem) return false;

    if (ctrl.cartDisable)
    {
        ramEnabled = false;
        cart->setExROMLine(true);
        cart->setGameLine(true);
        mem->setROMLOverlayIsRAM(false);
        return true;
    }

    ramEnabled = ctrl.ramEnable;
    mem->setROMLOverlayIsRAM(ramEnabled);

    cart->setExROMLine(ctrl.exromHigh);
    cart->setGameLine(!ctrl.gameLow);

    const uint8_t wantBank = (ctrl.bank & 0x03);
    if (loadedBank != wantBank)
    {
        if (!loadIntoMemory(wantBank))
            return false;

        loadedBank = wantBank;
    }

    selectedBank = wantBank;
    return true;
}

void AtomicPowerMapper::pressFreeze()
{
    if (!cart) return;

    if (!freezeActive)
    {
        freezeActive  = true;
        preFreezeRaw  = ctrl.raw;
        preFreezeBank = selectedBank;
    }

    ctrl.raw = 0x00;
    ctrl.decode();

    applyMappingAfterLoad();
    cart->requestCartridgeNMI();
}

void AtomicPowerMapper::pressReset()
{
    freezeActive = false;
    applyMappingAfterLoad();

    if (cart)
        cart->requestWarmReset();
}
