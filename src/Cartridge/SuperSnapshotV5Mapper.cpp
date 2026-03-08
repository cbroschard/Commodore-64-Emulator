// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/SuperSnapshotV5Mapper.h"

SuperSnapshotV5Mapper::SuperSnapshotV5Mapper() :
    selectedBank(0xFF)
{

}

SuperSnapshotV5Mapper::~SuperSnapshotV5Mapper() = default;

void SuperSnapshotV5Mapper::SS5Control::save(StateWriter& wrtr) const
{
    wrtr.writeU8(raw);
}

bool SuperSnapshotV5Mapper::SS5Control::load(StateReader& rdr)
{
    if (!rdr.readU8(raw))           return false;
    return true;
}

void  SuperSnapshotV5Mapper::SS5Control::decode()
{
    // bit 3: snapshot memory/register enable (0 enabled, 1 disabled)
    enabled = (raw & 0x08) == 0;

    // bit 1: controls EXROM line (INVERTED!)
    // 0 -> EXROM=1 (high), 1 -> EXROM=0 (low/asserted)
    exromHigh = (raw & 0x02) == 0;

    // bit 0: controls GAME line (0 -> GAME low, 1 -> GAME high)
    gameLow = (raw & 0x01) == 0;

    // Bank bits from SA14/SA15/(SA16)
    bank = 0;
    bank |= (raw >> 2) & 0x01;          // SA14
    bank |= ((raw >> 4) & 0x01) << 1;   // SA15
    bank |= ((raw >> 5) & 0x01) << 2;   // SA16
}

void SuperSnapshotV5Mapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("SSV5");
    wrtr.writeU32(1); // version

    ctrl.save(wrtr);

    wrtr.endChunk();
}

bool SuperSnapshotV5Mapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "SSV5", 4) == 0)
    {
        rdr.enterChunkPayload(chunk);

        uint32_t ver = 0;
        if (!rdr.readU32(ver))          { rdr.exitChunkPayload(chunk); return false; }
        if (ver != 1)                   { rdr.exitChunkPayload(chunk); return false; }

        if (!ctrl.load(rdr))            { rdr.exitChunkPayload(chunk); return false; }


        // Apply immediately
        ctrl.decode();
        selectedBank = 0xFF; // force reload of bank contents on state load
        if (!applyMappingAfterLoad())   { rdr.exitChunkPayload(chunk); return false; }

        rdr.exitChunkPayload(chunk);
        return true;
    }
    // Not our chunk
    return false;
}

const char* SuperSnapshotV5Mapper::getButtonName(uint32_t buttonIndex) const
{
    switch (buttonIndex)
    {
        case 0: return "Freeze";
        case 1: return "Reset";
        default: return "";
    }
}

void SuperSnapshotV5Mapper::pressButton(uint32_t buttonIndex)
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

uint8_t SuperSnapshotV5Mapper::read(uint16_t address)
{
    if (address >= 0xDE00 && address <= 0xDEFF)
    {
        if (!ctrl.enabled) return 0xFF;
        return ctrl.raw;
    }

    return 0xFF;
}

void SuperSnapshotV5Mapper::write(uint16_t address, uint8_t value)
{
    if (address >= 0xDE00 && address <= 0xDEFF)
    {
        ctrl.raw = value;
        ctrl.decode();
        (void)applyMappingAfterLoad();
    }
}

bool SuperSnapshotV5Mapper::loadIntoMemory(uint8_t bank)
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

        // 16K chip (rare for SSv5, but handle gracefully)
        if (s.data.size() == 0x4000)
        {
            // $8000-$9FFF -> LO
            for (size_t i = 0; i < 0x2000; ++i)
                mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::LO);

            // $A000-$BFFF -> HI
            for (size_t i = 0x2000; i < 0x4000; ++i)
                mem->writeCartridge(static_cast<uint16_t>(i - 0x2000), s.data[i], cartLocation::HI);

            // Mirror first 8K into $E000 for Ultimax safety
            for (size_t i = 0; i < 0x2000; ++i)
                mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::HI_E000);

            continue;
        }

        // 8K chip sections are the normal case
        if (s.data.size() == 0x2000)
        {
            const uint16_t la = s.loadAddress;

            if (la == 0x8000)
            {
                // ROML
                for (size_t i = 0; i < 0x2000; ++i)
                    mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::LO);

                // Mirror into $E000 to keep Ultimax vectors stable
                for (size_t i = 0; i < 0x2000; ++i)
                    mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::HI_E000);
                continue;
            }
            else if (la == 0xA000)
            {
                // ROMH (if present in CRT)
                for (size_t i = 0; i < 0x2000; ++i)
                    mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::HI);
                continue;
            }
            else if (la == 0xE000)
            {
                // Explicit $E000
                for (size_t i = 0; i < 0x2000; ++i)
                    mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::HI_E000);
                continue;
            }

            // Fallback -> treat as ROML + mirror
            for (size_t i = 0; i < 0x2000; ++i)
                mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::LO);
            for (size_t i = 0; i < 0x2000; ++i)
                mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::HI_E000);
            continue;
        }

        // Fallback clamp
        const size_t size = std::min(s.data.size(), static_cast<size_t>(0x2000));
        for (size_t i = 0; i < size; ++i)
            mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::LO);
        for (size_t i = 0; i < size; ++i)
            mem->writeCartridge(static_cast<uint16_t>(i), s.data[i], cartLocation::HI_E000);
    }

    return any;
}

bool SuperSnapshotV5Mapper::applyMappingAfterLoad()
{
    if (!mem || !cart)
        return false;

    if (!ctrl.enabled)
    {
        mem->setROMLOverlayIsRAM(false);
        cart->setGameLine(true);
        cart->setExROMLine(true);
        return true;
    }

    const uint8_t newBank = (ctrl.bank & 0x03); // clamp to 4 banks for stock SSv5

    if (selectedBank != newBank)
    {
        if (!loadIntoMemory(newBank))
            return false;
    }

    cart->setGameLine(!ctrl.gameLow);
    cart->setExROMLine(ctrl.exromHigh);

    mem->setROMLOverlayIsRAM(false);
    return true;
}

void SuperSnapshotV5Mapper::pressFreeze()
{
    if (!cart || !mem)
        return;

    ctrl.raw = 0x00;   // enabled, GAME low, EXROM high, bank 0
    ctrl.decode();

    (void)loadIntoMemory(0);

    cart->setExROMLine(true); // Ultimax
    cart->setGameLine(false);

    cart->requestCartridgeNMI();
}

void SuperSnapshotV5Mapper::pressReset()
{
    if (!cart || !mem)
        return;

    (void)applyMappingAfterLoad();
    cart->requestWarmReset();
}
