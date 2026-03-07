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
    processor(nullptr),
    freezePending(false),
    freezeActive(false),
    registersLocked(false),
    de01Locked(false),
    flashMode(false)
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

        ctrl.decode(flashMode);

        if (!applyMappingAfterLoad())       { rdr.exitChunkPayload(chunk); return false; }

        rdr.exitChunkPayload(chunk);
        return true;
    }

    return false;
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
        ctrl.de00 = value;
        applyMappingAfterLoad();
    }
    else if (address == 0xDE01)
    {
        ctrl.de01 = value;
        applyMappingAfterLoad();
    }
}

void RetroReplayMapper::pressFreeze()
{

}

bool RetroReplayMapper::loadIntoMemory(uint8_t bank)
{
    return true;
}

bool RetroReplayMapper::applyMappingAfterLoad()
{
    return true;
}
