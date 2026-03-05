// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/ExpertMapper.h"

ExpertMapper::ExpertMapper() :
    processor(nullptr),
    sw(SwitchPos::OFF),
    freezeCycles(10),
    freezeActive(false)
{

}

ExpertMapper::~ExpertMapper() = default;

uint32_t ExpertMapper::getSwitchPositionCount(uint32_t switchIndex) const
{
    if (switchIndex != 0) return 0;
    return 3; // OFF / ON / PRG
}

uint32_t ExpertMapper::getSwitchPosition(uint32_t switchIndex) const
{
    if (switchIndex != 0) return 0;
    return static_cast<uint32_t>(sw);
}

void ExpertMapper::setSwitchPosition(uint32_t switchIndex, uint32_t pos)
{
    if (switchIndex != 0) return;

    if (pos > 2) pos = 0;
    sw = static_cast<SwitchPos>(pos);

    // Immediately apply mapping when user flips switch in UI
    applyMappingAfterLoad();
}

const char* ExpertMapper::getSwitchPositionLabel(uint32_t switchIndex, uint32_t pos) const
{
    if (switchIndex != 0) return "?";

    switch (pos)
    {
        case 0: return "OFF";
        case 1: return "ON";
        case 2: return "PRG";
        default: return "?";
    }
}

void ExpertMapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("EXPT");
    wrtr.writeU32(1); // version
    wrtr.writeU8(static_cast<uint8_t>(sw));
    wrtr.writeBool(freezeActive);
    wrtr.writeI32(freezeCycles);
    wrtr.endChunk();
}

bool ExpertMapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "EXPT", 4) == 0)
    {
        rdr.enterChunkPayload(chunk);

        uint32_t ver = 0;
        if (!rdr.readU32(ver))                              { rdr.exitChunkPayload(chunk); return false; }
        if (ver != 1)                                       { rdr.exitChunkPayload(chunk); return false; }

        uint8_t swTmp = 0;
        if (!rdr.readU8(swTmp))                             { rdr.exitChunkPayload(chunk); return false; }
        if (swTmp > static_cast<uint8_t>(SwitchPos::PRG))
            swTmp = static_cast<uint8_t>(SwitchPos::OFF);
        sw = static_cast<SwitchPos>(swTmp);

        if (!rdr.readBool(freezeActive))                    { rdr.exitChunkPayload(chunk); return false; }

        int32_t fc = 0;
        if (!rdr.readI32(fc))                               { rdr.exitChunkPayload(chunk); return false; }
        freezeCycles = fc;

        // Apply Immediately
        if (!applyMappingAfterLoad())       { rdr.exitChunkPayload(chunk); return false; }

        rdr.exitChunkPayload(chunk);
        return true;
    }
   // Not our chunk
   return false;
}

uint8_t ExpertMapper::read(uint16_t address)
{
    return 0xFF;
}

void ExpertMapper::write(uint16_t address, uint8_t value)
{

}

void ExpertMapper::pressFreeze()
{
    if (!cart || !processor) return;

    freezeActive = true;
    freezeCycles = 10;

    // Turn on Ultimax mode for NMI
    cart->setExROMLine(true);
    cart->setGameLine(false);

    // NMI
    processor->pulseNMI();
}

void ExpertMapper::tick(uint32_t elapsedCycles)
{
    if (!freezeActive) return;

    // Guard: if elapsedCycles is huge, just clamp to 0
    if (elapsedCycles >= static_cast<uint32_t>(freezeCycles))
        freezeCycles = 0;
    else
        freezeCycles -= static_cast<int32_t>(elapsedCycles);

    if (freezeCycles <= 0)
    {
        freezeActive = false;
        applyMappingAfterLoad();
    }
}

bool ExpertMapper::loadIntoMemory(uint8_t bank)
{
    return true;
}

bool ExpertMapper::applyMappingAfterLoad()
{
    if (!cart || !mem) return false;

    // If we are still in the "hold Ultimax for vector fetch" phase:
    if (freezeActive)
    {
        cart->setExROMLine(true);    // Ultimax
        cart->setGameLine(false);

        // In Ultimax, vectors are fetched from $E000-$FFFF (CARTRIDGE_HI_E000).
        // Overlay that with cart RAM so $FFFA/$FFFB come from RAM.
        mem->setROMHOverlayIsRAM(true);

        // You *may* also leave ROML RAM enabled; doesn't hurt.
        mem->setROMLOverlayIsRAM(true);

        if (freezeCycles <= 0) freezeCycles = 10;
        return true;
    }

    // Normal (non-freeze) mapping depends on switch
    if (sw == SwitchPos::OFF)
    {
        // Cart disconnected
        cart->setExROMLine(true);
        cart->setGameLine(true);

        mem->setROMLOverlayIsRAM(false);
        mem->setROMHOverlayIsRAM(false);
    }
    else
    {
        // Cart present as 8K
        cart->setExROMLine(false);
        cart->setGameLine(true);

        // $8000-$9FFF should be cart RAM
        mem->setROMLOverlayIsRAM(true);

        // Not in Ultimax, so ROMH overlay normally off
        mem->setROMHOverlayIsRAM(false);
    }

    return true;
}
