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
    freezeButtonPressed(false),
    freezePending(false),
    freezeActive(false),
    cartActive(true),
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
    ramSelected     = (de00 & 0x20) != 0;
    leaveFreeze     = (de00 & 0x40) != 0;

    accessoryEnable = (de01 & 0x01) != 0;
    allowBank       = (de01 & 0x02) != 0;
    noFreeze        = (de01 & 0x04) != 0;
    reuCompat       = (de01 & 0x40) != 0;

    uint8_t bank =
        ((de00 >> 3) & 0x01) |
        (((de00 >> 4) & 0x01) << 1) |
        (((de00 >> 7) & 0x01) << 2);

    if (flashMode)
        bank |= (((de01 >> 5) & 0x01) << 3);

    romBank = bank;

    ramBank = bank & 0x03;

    const bool gameBit   = (de00 & 0x01) != 0;
    const bool exromBit  = (de00 & 0x02) != 0;

    if (ramSelected)
    {
        mapMode = allowBank ? RRMapMode::Ram16K : RRMapMode::Ram8K;
        return;
    }

    if (!gameBit && !exromBit)
    {
        mapMode = RRMapMode::Normal16K;
    }
    else if (!gameBit && exromBit)
    {
        mapMode = RRMapMode::Normal8K;
    }
    else
    {
        mapMode = RRMapMode::Ultimax;
    }
}

void RetroReplayMapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("RRPY");
    wrtr.writeU32(1); // version

    ctrl.save(wrtr);
    wrtr.writeBool(freezeButtonPressed);
    wrtr.writeBool(freezePending);
    wrtr.writeBool(freezeActive);
    wrtr.writeBool(cartActive);
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
        if (!rdr.readU32(ver))                  { rdr.exitChunkPayload(chunk); return false; }
        if (ver != 1)                           { rdr.exitChunkPayload(chunk); return false; }

        if (!ctrl.load(rdr))                    { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(freezeButtonPressed)) { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(freezePending))       { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(freezeActive))        { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(cartActive))          { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(registersLocked))     { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(de01Locked))          { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(flashMode))           { rdr.exitChunkPayload(chunk); return false; }

        uint32_t fdc = 0;
        if (!rdr.readU32(fdc))                  { rdr.exitChunkPayload(chunk); return false; }
        freezeDelayCycles = fdc;

        ctrl.decode(flashMode);

        if (!applyMappingAfterLoad())           { rdr.exitChunkPayload(chunk); return false; }

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
    if (address != 0xDE00 && address != 0xDE01)
        return 0xFF;

    ctrl.decode(flashMode);

    uint8_t v = 0x00;

    // bit 0 = flash mode active
    if (flashMode)
        v |= 0x01;

    // bit 1 = AllowBank feedback
    if (ctrl.allowBank)
        v |= 0x02;

    // bit 2 = Freeze button pressed
    if (freezeButtonPressed)
        v |= 0x04;

    // bits 3,4,7 = bank 13,14,15 feedback from rr_bank
    v |= (ctrl.romBank & 0x03) << 3;   // bits 0-1 -> 3-4
    v |= (ctrl.romBank & 0x04) << 5;   // bit 2 -> 7

    // bit 5 = bank 16 feedback in flash mode
    if (flashMode && (ctrl.romBank & 0x08))
        v |= 0x20;

    // bit 6 = REU mapping feedback
    if (ctrl.reuCompat)
        v |= 0x40;

    return v;
}

void RetroReplayMapper::write(uint16_t address, uint8_t value)
{
    if (address == 0xDE00)
    {
        if (registersLocked)
            return;

        ctrl.de00 = value;

        // Once frozen, GAME/EXROM bits should not really take effect until bit 6 acknowledges release.
        if (freezeActive)
        {
            if (value & 0x40)
            {
                freezeActive = false;
                freezePending = false;
                freezeDelayCycles = 0;
            }
            else
            {
                // keep frozen mapping until release
                ctrl.de00 = static_cast<uint8_t>((ctrl.de00 & ~0x03) | 0x01);
            }
        }

        if (value & 0x04)
        {
            registersLocked = true;
            cartActive      = false;
        }

        (void)applyMappingAfterLoad();
        return;
    }

    if (address == 0xDE01)
    {
        if (registersLocked)
            return;

        if (flashMode)
        {
            // In flash mode, allowBank/noFreeze may still change,
            // REU mapping must remain clear.
            ctrl.de01 = value & static_cast<uint8_t>(~0x40);
        }
        else
        {
            if (!de01Locked)
            {
                // First write: all bits accepted except bit 5, which VICE keeps 0 outside flash mode.
                ctrl.de01 = value & static_cast<uint8_t>(~0x20);
                de01Locked = true;
            }
            else
            {
                // Later writes:
                // preserve write-once bits 1,2,6
                // update only bits 0,3,4,7
                const uint8_t preserved = ctrl.de01 & 0x46;   // bits 1,2,6
                const uint8_t writable  = value     & 0x99;   // bits 0,3,4,7
                ctrl.de01 = preserved | writable;
            }
        }

        ctrl.de00 = static_cast<uint8_t>((ctrl.de00 & ~0x98) | (ctrl.de01 &  0x98));

        (void)applyMappingAfterLoad();
        return;
    }
}

bool RetroReplayMapper::loadIntoMemory(uint8_t bank)
{
    if (!mem || !cart)
        return false;

    // In Retro Replay freeze mode, bank 0 is active directly after Freeze.
    const uint8_t effectiveBank = freezeActive ? 0 : bank;

    if (!freezeActive &&
        (ctrl.mapMode == RRMapMode::Ram8K || ctrl.mapMode == RRMapMode::Ram16K))
    {
        return true;
    }

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

        if (s.data.size() == 0x4000)
        {
            for (size_t i = 0; i < 0x2000; ++i)
                mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::LO);

            for (size_t i = 0; i < 0x2000; ++i)
                mem->writeCartridge(static_cast<uint16_t>(i), s.data[i + 0x2000], cartLocation::HI);

            if (freezeActive)
            {
                for (size_t i = 0; i < 0x2000; ++i)
                    mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::HI_E000);
            }

            continue;
        }

        if (s.data.size() == 0x2000)
        {
            const uint16_t la = s.loadAddress;

            if (la == 0x8000)
            {
                for (size_t i = 0; i < 0x2000; ++i)
                    mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::LO);

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
                for (size_t i = 0; i < 0x2000; ++i)
                    mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::LO);

                if (freezeActive)
                {
                    for (size_t i = 0; i < 0x2000; ++i)
                        mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::HI_E000);
                }

                continue;
            }
        }

        const size_t size = std::min(s.data.size(), static_cast<size_t>(0x2000));

        for (size_t i = 0; i < size; ++i)
            mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::LO);

        if (freezeActive)
        {
            for (size_t i = 0; i < size; ++i)
                mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::HI_E000);
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

    freezeButtonPressed = false;
    freezeDelayCycles   = 0;
    freezePending       = false;
    freezeActive        = true;

    (void)applyMappingAfterLoad();
}

bool RetroReplayMapper::applyMappingAfterLoad()
{
    if (!cart || !mem)
        return false;

    if (!cartActive)
    {
        cart->setExROMLine(true);
        cart->setGameLine(true);
        cart->clearCartridge(cartLocation::LO);
        cart->clearCartridge(cartLocation::HI);
        cart->clearCartridge(cartLocation::HI_E000);
        return true;
    }

    ctrl.decode(flashMode);

    mem->setROMLOverlayIsRAM(false);
    mem->setROMHOverlayIsRAM(false);

    if (freezeActive)
    {
        cart->setExROMLine(false);
        cart->setGameLine(true);
        return loadIntoMemory(0);
    }

    switch (ctrl.mapMode)
    {
        case RRMapMode::Normal8K:
            cart->setExROMLine(true);
            cart->setGameLine(false);
            return loadIntoMemory(ctrl.romBank);

        case RRMapMode::Normal16K:
            cart->setExROMLine(false);
            cart->setGameLine(false);
            return loadIntoMemory(ctrl.romBank);

        case RRMapMode::Ultimax:
            cart->setExROMLine(false);
            cart->setGameLine(true);
            return loadIntoMemory(ctrl.romBank);

        case RRMapMode::Ram8K:
            cart->setExROMLine(true);
            cart->setGameLine(false);
            mem->setROMLOverlayIsRAM(true);
            return loadIntoMemory(ctrl.romBank);

        case RRMapMode::Ram16K:
            cart->setExROMLine(false);
            cart->setGameLine(false);
            mem->setROMLOverlayIsRAM(true);
            mem->setROMHOverlayIsRAM(true);
            return loadIntoMemory(ctrl.romBank);

        default:
            return false;
    }
}

void RetroReplayMapper::pressFreeze()
{
    if (!cart || !mem)
        return;

    ctrl.decode(flashMode);

    if (ctrl.noFreeze)
        return;

    if (freezePending || freezeActive)
        return;

    freezeButtonPressed     = true;
    freezePending           = true;
    freezeDelayCycles       = 3;
    cart->requestCartridgeNMI();
}

void RetroReplayMapper::pressReset()
{
    if (!cart || !mem) return;

    freezeButtonPressed = false;
    freezePending       = false;
    freezeActive        = false;
    cartActive          = true;
    registersLocked     = false;
    de01Locked          = false;
    freezeDelayCycles   = 0;

    ctrl.de00 &= static_cast<uint8_t>(~0x04);

    (void)applyMappingAfterLoad();

    cart->requestWarmReset();
}

void RetroReplayMapper::reset()
{
    freezeButtonPressed = false;
    freezePending       = false;
    freezeActive        = false;
    registersLocked     = false;
    cartActive          = true;
    de01Locked          = false;
    freezeDelayCycles   = 0;

    ctrl.de00           = 0x00;
    ctrl.de01           = 0x00;

    ctrl.decode(flashMode);

    if (mem)
    {
        mem->setROMLOverlayIsRAM(false);
        mem->setROMHOverlayIsRAM(false);
    }

    (void)applyMappingAfterLoad();
}
