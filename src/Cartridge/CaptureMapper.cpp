// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/CaptureMapper.h"
#include "Memory.h"

CaptureMapper::CaptureMapper() :
    mode(Mode::Normal),
    registersEnabled(false),
    romHEnabled(false)
{

}

CaptureMapper::~CaptureMapper() = default;

void CaptureMapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("CAPT");
    wrtr.writeU32(1);

    wrtr.writeU8(static_cast<uint8_t>(mode));
    wrtr.writeBool(registersEnabled);
    wrtr.writeBool(romHEnabled);

    wrtr.endChunk();
}

bool CaptureMapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "CAPT", 4) == 0)
    {
        rdr.enterChunkPayload(chunk);

        uint32_t ver = 0;
        if (!rdr.readU32(ver))                              { rdr.exitChunkPayload(chunk); return false; }
        if (ver != 1)                                       { rdr.exitChunkPayload(chunk); return false; }

        uint8_t modeU8 = 0;
        if (!rdr.readU8(modeU8))                            { rdr.exitChunkPayload(chunk); return false; }
        if (modeU8 > static_cast<uint8_t>(Mode::Freeze))    { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readBool(registersEnabled))                { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(romHEnabled))                     { rdr.exitChunkPayload(chunk); return false; }

        mode = static_cast<Mode>(modeU8);

        if (mode != Mode::Freeze)
        {
            registersEnabled = false;
            romHEnabled = false;
        }

        rdr.exitChunkPayload(chunk);
        return true;
    }
    // Not our chunk
    return false;
}

uint8_t CaptureMapper::read(uint16_t address)
{
    if (!cart || !mem)
        return 0xFF;

    // Capture RAM at $6000-$7FFF.
    if (mode == Mode::Freeze && address >= 0x6000 && address <= 0x7FFF)
    {
        return cart->readRAM(static_cast<size_t>(address - 0x6000));
    }

    // Control addresses must be checked before the broad ROMH range.
    if (registersEnabled && address == 0xFFF7)
    {
        romHEnabled = false;
        return cart ? cart->sampleDataBus() : 0xFF;
    }

    if (registersEnabled && address == 0xFFF8)
    {
        romHEnabled = true;

        return mem->readCartridge(0x1FF8, cartLocation::HI_E000);
    }

    // Capture high ROM during Ultimax mode.
    if (mode == Mode::Freeze && address >= 0xE000 && address <= 0xFFFF)
    {
        if (!romHEnabled)
            return cart ? cart->sampleDataBus() : 0xFF;

        const uint16_t offset = static_cast<uint16_t>(address - 0xE000);

        return mem->readCartridge(offset, cartLocation::HI_E000);
    }

    return 0xFF;
}

void CaptureMapper::write(uint16_t address, uint8_t value)
{
    if (!cart)
        return;

    if (mode == Mode::Freeze &&
        address >= 0x6000 &&
        address <= 0x7FFF)
    {
        const size_t offset =
            static_cast<size_t>(address - 0x6000);

        cart->writeRAM(offset, value);
        return;
    }

    if (!registersEnabled)
        return;

    if (address == 0xFFF7)
    {
        romHEnabled = false;
        return;
    }

    if (address == 0xFFF8)
    {
        romHEnabled = true;
    }
}

bool CaptureMapper::applyMappingAfterLoad()
{
    if (!cart)
        return false;

    switch (mode)
    {
        case Mode::Normal:
        {
            // Normal 8K cartridge mode.
            cart->setGameLine(true);
            cart->setExROMLine(false);
            return true;
        }

        case Mode::Freeze:
        {
            // Ultimax mode.
            cart->setGameLine(false);
            cart->setExROMLine(true);
            return true;
        }
    }

    return false;
}

bool CaptureMapper::loadIntoMemory(uint8_t bank)
{
    (void)bank;

    if (!cart || !mem)
        return false;

    cart->clearCartridge(cartLocation::LO);
    cart->clearCartridge(cartLocation::HI);
    cart->clearCartridge(cartLocation::HI_E000);

    for (const auto& section : cart->getChipSections())
    {
        if (section.loadAddress != 0x8000)
            continue;

        if (section.data.size() != 0x2000)
            continue;

        for (size_t i = 0; i < 0x2000; ++i)
        {
            mem->writeCartridge(static_cast<uint16_t>(i), section.data[i], cartLocation::LO);

            mem->writeCartridge(static_cast<uint16_t>(i), section.data[i], cartLocation::HI_E000);
        }

        return applyMappingAfterLoad();
    }

    return false;
}

void CaptureMapper::reset()
{
    registersEnabled = false;
    romHEnabled = false;

    setMode(Mode::Normal);
}

const char* CaptureMapper::getButtonName(uint32_t buttonIndex) const
{
    switch(buttonIndex)
    {
        case 0:
            return "Freeze";
        default:
            return "";
    }
}

void CaptureMapper::pressButton(uint32_t buttonIndex)
{
    switch(buttonIndex)
    {
        case 0:
            pressFreeze();
            break;
        default:
            break;
    }
}

void CaptureMapper::pressFreeze()
{
    if (!cart)
        return;

    registersEnabled = true;
    romHEnabled = true;

    if (!setMode(Mode::Freeze))
        return;

    cart->requestCartridgeNMI();
}

bool CaptureMapper::setMode(Mode newMode)
{
    mode = newMode;
    return applyMappingAfterLoad();
}

bool CaptureMapper::cpuMemoryHandledByMapper(uint16_t address) const
{
    if (!cart)
        return false;

    // Capture RAM is visible here while the cartridge is in freeze/Ultimax mode.
    if (mode == Mode::Freeze && address >= 0x6000 && address <= 0x7FFF)
        return true;

    // Capture control accesses.
    if (registersEnabled && (address == 0xFFF7 || address == 0xFFF8))
        return true;

    return false;
}

bool CaptureMapper::romReadHandledByMapper(uint16_t address) const
{
    return mode == Mode::Freeze && address >= 0xE000 && address <= 0xFFFF;
}
