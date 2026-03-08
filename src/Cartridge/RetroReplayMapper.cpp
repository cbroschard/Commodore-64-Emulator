// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/RetroReplayMapper.h"

RetroReplayMapper::RetroReplayMapper() :
    freezePending(false),
    freezeActive(false),
    registersLocked(false),
    de01Locked(false),
    flashMode(false),
    freezeDelayCycles(0)
{

}

RetroReplayMapper::~RetroReplayMapper() = default;

void RetroReplayMapper::RRControl::save(StateWriter& wrtr) const
{
    wrtr.writeU8(de00);
    wrtr.writeU8(de01);
}

bool RetroReplayMapper::RRControl::load(StateReader& rdr)
{
    if (!rdr.readU8(de00))
        return false;
    if (!rdr.readU8(de01))
        return false;
    return true;
}

void RetroReplayMapper::RRControl::decode(bool flashMode)
{
    gameAsserted = (de00 & 0x01) != 0;
    exromAsserted = (de00 & 0x02) == 0;
    cartDisabled = (de00 & 0x04) != 0;

    ramSelected = (de00 & 0x20) != 0;
    leaveFreeze = (de00 & 0x40) != 0;

    accessoryEnable = (de01 & 0x01) != 0;
    allowBank       = (de01 & 0x02) != 0;
    noFreeze        = (de01 & 0x04) != 0;
    reuCompat       = (de01 & 0x40) != 0;

    uint8_t bank =
        ((de00 >> 3) & 0x01) |
        (((de00 >> 4) & 0x01) << 1) |
        (((de00 >> 7) & 0x01) << 2);

    if (flashMode)
    {
        bank |= (((de01 >> 5) & 0x01) << 3);
    }

    romBank = bank;
}

void RetroReplayMapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("RRPY");
    wrtr.writeU32(1); // version

    ctrl.save(wrtr);
    wrtr.writeBool(freezePending);
    wrtr.writeBool(freezeActive);
    wrtr.writeBool(registersLocked);
    wrtr.writeBool(de01Locked);
    wrtr.writeBool(flashMode);
    wrtr.writeU32(freezeDelayCycles);

    wrtr.endChunk();
}

bool RetroReplayMapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "RRPY", 4) == 0)
    {
        rdr.enterChunkPayload(chunk);

        uint32_t ver = 0;
        if (!rdr.readU32(ver))              { rdr.exitChunkPayload(chunk); return false; }
        if (ver != 1)                       { rdr.exitChunkPayload(chunk); return false; }

        if (!ctrl.load(rdr))                { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(freezePending))   { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(freezeActive))    { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(registersLocked)) { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(de01Locked))      { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(flashMode))       { rdr.exitChunkPayload(chunk); return false; }

        uint32_t fdc = 0;
        if (!rdr.readU32(fdc))              { rdr.exitChunkPayload(chunk); return false; }
        freezeDelayCycles = fdc;

        ctrl.decode(flashMode);

        if (!applyMappingAfterLoad())       { rdr.exitChunkPayload(chunk); return false; }

        rdr.exitChunkPayload(chunk);
        return true;
    }

    return false;
}

const char* RetroReplayMapper::getButtonName(uint32_t buttonIndex) const
{
    switch (buttonIndex)
    {
        case 0: return "Freeze";
        case 1: return "Reset";
        default: return "";
    }
}

void RetroReplayMapper::pressButton(uint32_t buttonIndex)
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

uint8_t RetroReplayMapper::read(uint16_t address)
{
    // If the cartridge has been disabled, it should disappear from the bus.
    if (ctrl.cartDisabled)
        return 0xFF;

    // IO1 registers
    if (address == 0xDE00)
        return ctrl.de00;
    else if (address == 0xDE01)
        return ctrl.de01;

    return 0xFF;
}

void RetroReplayMapper::write(uint16_t address, uint8_t value)
{
    if (address == 0xDE00)
    {
        if (registersLocked)
            return;

        ctrl.de00 = value;

        ctrl.decode(flashMode);
        if (freezeActive && ctrl.leaveFreeze)
        {
            freezeActive = false;
            freezePending = false;
            freezeDelayCycles = 0;
        }

        (void)applyMappingAfterLoad();
    }
    else if (address == 0xDE01)
    {
        if (registersLocked || de01Locked)
            return;

        ctrl.de01 = value;
        (void)applyMappingAfterLoad();
    }
}

bool RetroReplayMapper::loadIntoMemory(uint8_t bank)
{
    if (!mem || !cart)
        return false;

    // In Retro Replay freeze mode, bank 0 is active directly after Freeze.
    const uint8_t effectiveBank = freezeActive ? 0 : bank;

    cart->clearCartridge(cartLocation::LO);
    cart->clearCartridge(cartLocation::HI);
    cart->clearCartridge(cartLocation::HI_E000);

    const auto& sections = cart->getChipSections();

    bool any = false;
    for (const auto& s : sections)
    {
        if (s.bankNumber != effectiveBank)
            continue;

        any = true;

        // 16K image: split into $8000 and $A000
        if (s.data.size() == 0x4000)
        {
            // $8000-$9FFF -> ROML
            for (size_t i = 0; i < 0x2000; ++i)
                mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::LO);

            // $A000-$BFFF -> ROMH
            for (size_t i = 0; i < 0x2000; ++i)
                mem->writeCartridge(static_cast<uint16_t>(i), s.data[i + 0x2000], cartLocation::HI);

            if (freezeActive)
            {
                for (size_t i = 0; i < 0x2000; ++i)
                    mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::HI_E000);
            }

            continue;
        }

        // 8K image: route according to declared load address
        if (s.data.size() == 0x2000)
        {
            const uint16_t la = s.loadAddress;

            if (la == 0x8000)
            {
                for (size_t i = 0; i < 0x2000; ++i)
                    mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::LO);

                // In freeze mode, RR needs ROM visible at $E000-$FFFF.
                if (freezeActive)
                {
                    for (size_t i = 0; i < 0x2000; ++i)
                        mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::HI_E000);
                }

                continue;
            }
            else if (la == 0xA000)
            {
                for (size_t i = 0; i < 0x2000; ++i)
                    mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::HI);

                continue;
            }
            else if (la == 0xE000)
            {
                for (size_t i = 0; i < 0x2000; ++i)
                    mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::HI_E000);

                continue;
            }
            else
            {
                // Fallback: treat unknown 8K as ROML
                for (size_t i = 0; i < 0x2000; ++i)
                    mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::LO);

                // In freeze mode, ensure the freezer area exists.
                if (freezeActive)
                {
                    for (size_t i = 0; i < 0x2000; ++i)
                        mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::HI_E000);
                }

                continue;
            }
        }

        // Generic fallback: clamp to 8K and load somewhere sensible.
        {
            const size_t size = std::min(s.data.size(), static_cast<size_t>(0x2000));

            for (size_t i = 0; i < size; ++i)
                mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::LO);

            if (freezeActive)
            {
                for (size_t i = 0; i < size; ++i)
                    mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::HI_E000);
            }
        }
    }

    return any;
}

void RetroReplayMapper::tick(uint32_t elapsedCycles)
{
    if (!freezePending || elapsedCycles == 0)
        return;

    if (freezeDelayCycles > elapsedCycles)
    {
        freezeDelayCycles -= elapsedCycles;
        return;
    }

    freezeDelayCycles = 0;
    freezePending = false;
    freezeActive  = true;

    (void)applyMappingAfterLoad();
}

bool RetroReplayMapper::applyMappingAfterLoad()
{
    if (!cart || !mem)
        return false;

    ctrl.decode(flashMode);

    // If freeze is only pending, keep the temporary entry mapping.
    if (freezePending)
    {
        cart->setExROMLine(false);
        cart->setGameLine(true);
        return true;
    }

    // Actual freeze mode after NMI acknowledge.
    if (freezeActive)
    {
        cart->setExROMLine(false);
        cart->setGameLine(true);

        return loadIntoMemory(0);
    }

    // Normal disabled state.
    if (ctrl.cartDisabled)
    {
        cart->setExROMLine(true);
        cart->setGameLine(true);

        cart->clearCartridge(cartLocation::LO);
        cart->clearCartridge(cartLocation::HI);
        cart->clearCartridge(cartLocation::HI_E000);

        return true;
    }

    // Normal mapping from decoded DE00 bits.
    cart->setExROMLine(ctrl.exromAsserted);
    cart->setGameLine(ctrl.gameAsserted);

    return loadIntoMemory(ctrl.romBank);
}

void RetroReplayMapper::pressFreeze()
{
    if (!cart || !mem)
        return;

    if (freezePending || freezeActive)
        return;

    freezePending = true;
    freezeDelayCycles = 3;

    (void)applyMappingAfterLoad();
    cart->requestCartridgeNMI();
}

void RetroReplayMapper::pressReset()
{
    if (!cart || !mem) return;

    freezePending    = false;
    freezeActive     = false;
    registersLocked  = false;
    de01Locked       = false;
    flashMode        = false;
    freezeDelayCycles = 0;

    (void)applyMappingAfterLoad();

    cart->requestWarmReset();
}
