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
    const bool disabled  = (de00 & 0x04) != 0;

    if (disabled)
    {
        mapMode = RRMapMode::Disabled;
        return;
    }

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
    if (ctrl.mapMode == RRMapMode::Disabled)
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

        if (freezeActive && (value & 0x40))
        {
            freezeActive = false;
            freezePending = false;
            freezeDelayCycles = 0;
        }

        (void)applyMappingAfterLoad();
        return;
    }

    if (address == 0xDE01)
    {
        if (registersLocked || de01Locked)
            return;

        ctrl.de01 = value;

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

    mem->setROMLOverlayIsRAM(false);
    mem->setROMHOverlayIsRAM(false);

    if (freezePending || freezeActive)
    {
        cart->setExROMLine(false);
        cart->setGameLine(true);
        return loadIntoMemory(0);
    }

    switch (ctrl.mapMode)
    {
        case RRMapMode::Disabled:
            cart->setExROMLine(true);
            cart->setGameLine(true);
            cart->clearCartridge(cartLocation::LO);
            cart->clearCartridge(cartLocation::HI);
            cart->clearCartridge(cartLocation::HI_E000);
            return true;

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

void RetroReplayMapper::reset()
{
    freezePending = false;
    freezeActive = false;
    registersLocked = false;
    de01Locked = false;
    flashMode = false;
    freezeDelayCycles = 0;

    ctrl.de00 = 0x00;
    ctrl.de01 = 0x00;
    ctrl.decode(flashMode);

    if (mem)
    {
        mem->setROMLOverlayIsRAM(false);
        mem->setROMHOverlayIsRAM(false);
    }

    (void)applyMappingAfterLoad();
}
