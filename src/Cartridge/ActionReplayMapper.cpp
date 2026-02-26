// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge/ActionReplayMapper.h"
#include "Cartridge.h"
#include "CPU.h"

ActionReplayMapper::ActionReplayMapper() :
    processor(nullptr),
    selectedBank(0),
    io1Enabled(false),
    io2RoutesToRam(false),
    freezeActive(false),
    preFreezeSelectedBank(0)
{

}

ActionReplayMapper::~ActionReplayMapper() = default;

void ActionReplayMapper::ARControl::save(StateWriter& wrtr) const
{
    wrtr.writeU8(raw);
    wrtr.writeBool(cartDisabled);
    wrtr.writeBool(ramAtROML);
    wrtr.writeBool(freezeReset);
    wrtr.writeU8(bank);
    wrtr.writeBool(exromHigh);
    wrtr.writeBool(gameLow);
}

bool ActionReplayMapper::ARControl::load(StateReader& rdr)
{
    if (!rdr.readU8(raw))               return false;
    if (!rdr.readBool(cartDisabled))    return false;
    if (!rdr.readBool(ramAtROML))       return false;
    if (!rdr.readBool(freezeReset))     return false;
    if (!rdr.readU8(bank))              return false;
    if (!rdr.readBool(exromHigh))       return false;
    if (!rdr.readBool(gameLow))         return false;
    return true;
}

void ActionReplayMapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("ARPY");
    wrtr.writeU32(2); // version

    ctrl.save(wrtr);
    wrtr.writeU8(selectedBank);
    wrtr.writeBool(io1Enabled);
    wrtr.writeBool(io2RoutesToRam);
    wrtr.writeBool(freezeActive);
    wrtr.writeU8(preFreezeSelectedBank);

    wrtr.endChunk();
}

bool ActionReplayMapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "ARPY", 4) == 0)
    {
        rdr.enterChunkPayload(chunk);

        uint32_t ver = 0;
        if (!rdr.readU32(ver))                  { rdr.exitChunkPayload(chunk); return false; }
        if (ver != 2)                           { rdr.exitChunkPayload(chunk); return false; }

        if (!ctrl.load(rdr))                    { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU8(selectedBank))          { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(io1Enabled))          { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(io2RoutesToRam))      { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(freezeActive))        { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU8(preFreezeSelectedBank)) { rdr.exitChunkPayload(chunk); return false; }

        // Apply side effects
        if (!applyMappingAfterLoad())           { rdr.exitChunkPayload(chunk); return false; }

        rdr.exitChunkPayload(chunk);
        return true;
    }

    // Not our chunk
    return false;
}

uint8_t ActionReplayMapper::read(uint16_t address)
{
    // If cartridge disabled, $DE00 shouldn't respond
    if (ctrl.cartDisabled && address >= 0xDE00 && address <= 0xDEFF)
        return 0xFF;

    if (address >= 0xDE00 && address <= 0xDEFF)
    {
        if (ctrl.cartDisabled || !io1Enabled) return 0xFF;
        return ctrl.raw; // simple readback
    }

    // IO2 RAM window when bit5 set:
    // $DF00-$DFFF mirrors $9F00-$9FFF (top 256 bytes of ROML RAM window)
    if (address >= 0xDF00 && address <= 0xDFFF)
    {
        if (io2RoutesToRam && cart && cart->hasCartridgeRAM())
        {
            const size_t off = 0x1F00u + static_cast<size_t>((address - 0xDF00) & 0x00FF);
            return cart->readRAM(off);
        }
        return 0xFF;
    }

    return 0xFF;
}

void ActionReplayMapper::write(uint16_t address, uint8_t value)
{
    // If cartridge disabled, $DE00 shouldn't respond.
    // (In real hardware it's not decoded; in emu we just ignore)
    if (ctrl.cartDisabled && address >= 0xDE00 && address <= 0xDEFF)
        return;

    if (address >= 0xDE00 && address <= 0xDEFF)
    {
        ctrl.raw = value;

        ctrl.gameLow      = (value & (1 << 0)) != 0;   // 1=/GAME low (active)
        ctrl.exromHigh    = (value & (1 << 1)) != 0;   // 1=/EXROM high (inactive)
        ctrl.cartDisabled = (value & (1 << 2)) != 0;   // 1=disable $DE00
        ctrl.freezeReset  = (value & (1 << 6)) != 0;   // 1=reset FREEZE-mode
        ctrl.ramAtROML    = (value & (1 << 5)) != 0;   // 1=RAM at ROML + IO2 window

        // Bank select: bit3=A13 (LSB), bit4=A14 (MSB). Bit7 is unused.
        const uint8_t a13 = (value >> 3) & 1;
        const uint8_t a14 = (value >> 4) & 1;
        ctrl.bank = (a14 << 1) | a13;

        applyMappingFromControl();
        return;
    }

    if (address >= 0xDF00 && address <= 0xDFFF)
    {
        if (io2RoutesToRam && cart && cart->hasCartridgeRAM())
        {
            const size_t off = 0x1F00u + static_cast<size_t>((address - 0xDF00) & 0x00FF);
            cart->writeRAM(off, value);
        }
        return;
    }
}

bool ActionReplayMapper::applyMappingAfterLoad()
{
    if (!mem || !cart) return false;

    if (!loadIntoMemory(ctrl.bank))
        return false;

    applyMappingFromControl();
    return true;
}

bool ActionReplayMapper::loadIntoMemory(uint8_t bank)
{
    if (!mem || !cart) return false;

    selectedBank = bank;

    cart->clearCartridge(cartLocation::LO);
    cart->clearCartridge(cartLocation::HI);

    const auto& sections = cart->getChipSections();

    bool any = false;
    for (const auto& s : sections)
    {
        if (s.bankNumber != bank) continue;
        any = true;

        // If a section is 16K, split it LO/HI like you already do
        if (s.data.size() == 0x4000)
        {
            for (size_t i = 0; i < 0x2000; ++i)
                mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::LO);
            for (size_t i = 0x2000; i < 0x4000; ++i)
                mem->writeCartridge(static_cast<uint16_t>(i - 0x2000), s.data[i], cartLocation::HI);
            continue;
        }

        // If a section is 8K, route it based on loadAddress
        if (s.data.size() == 0x2000)
        {
            const uint16_t la = s.loadAddress;
            const cartLocation loc =
                (la == 0x8000) ? cartLocation::LO :
                (la == 0xA000 || la == 0xE000) ? cartLocation::HI :
                cartLocation::LO; // fallback

            for (size_t i = 0; i < 0x2000; ++i)
                mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], loc);

            continue;
        }

        // Fallback: clamp and load as LO
        const size_t size = std::min(s.data.size(), static_cast<size_t>(0x2000));
        for (size_t i = 0; i < size; ++i)
            mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::LO);
    }

    return any;
}

void ActionReplayMapper::applyMappingFromControl()
{
    if (ctrl.cartDisabled)
    {
        io1Enabled = false;
        io2RoutesToRam = false;

        if (mem)
            mem->setROMLOverlayIsRAM(false);

        return;
    }

    // Bit6: reset FREEZE-mode
    if (ctrl.freezeReset)
    {
        ctrl.freezeReset = false; // consume strobe
        clearFreezeMode();
        return;
    }

    io1Enabled = !ctrl.cartDisabled;

    // Apply cartridge lines:
    // ctrl.exromHigh == true => /EXROM high (inactive)
    // ctrl.gameLow   == true => /GAME low (active) => gameHigh is inverse
    const bool exromHigh = ctrl.exromHigh;
    const bool gameHigh  = !ctrl.gameLow;

    cart->setExROMLine(exromHigh);
    cart->setGameLine(gameHigh);

    // Bank ROM if needed
    if (selectedBank != ctrl.bank)
    {
        selectedBank = ctrl.bank;
        (void)loadIntoMemory(selectedBank);
    }

    // Bit5: RAM at ROML ($8000-$9FFF) + IO2 window enabled
    if (mem)
        mem->setROMLOverlayIsRAM(ctrl.ramAtROML);

    io2RoutesToRam = ctrl.ramAtROML;
}

void ActionReplayMapper::pressFreeze()
{
    if (!cart || !mem || !processor)
        return;

    // Snapshot current mapping so we can return later
    if (!freezeActive)
    {
        preFreezeCtrl = ctrl;
        preFreezeSelectedBank = selectedBank;
    }
    freezeActive = true;

    // Freeze entry mapping
    ctrl.cartDisabled = false;
    ctrl.bank = 0;
    ctrl.exromHigh = false; // /EXROM low
    ctrl.gameLow   = false; // /GAME high
    ctrl.ramAtROML = false;

    (void)loadIntoMemory(ctrl.bank);
    applyMappingFromControl();

    processor->pulseNMI();
}

void ActionReplayMapper::clearFreezeMode()
{
    if (!freezeActive)
        return;

    freezeActive = false;

    ctrl = preFreezeCtrl;
    ctrl.freezeReset = false;
    selectedBank = preFreezeSelectedBank;

    (void)loadIntoMemory(selectedBank);
    applyMappingFromControl();
}
