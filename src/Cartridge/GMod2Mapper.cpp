// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/GMod2Mapper.h"

GMod2Mapper::GMod2Mapper() :
    flashData(FLASH_SIZE, 0xFF),
    flashDirty(false),
    flashInitialized(false),
    selectedBank(0)
{

}

GMod2Mapper::~GMod2Mapper() = default;

void GMod2Mapper::G2Control::save(StateWriter& wrtr) const
{
    wrtr.writeU8(raw);
}

bool GMod2Mapper::G2Control::load(StateReader& rdr)
{
    if (!rdr.readU8(raw))   return false;

    decode();
    return true;
}

void GMod2Mapper::G2Control::decode()
{
    romBank     = raw & 0x3F;         // bits 0-5

    di          = (raw & 0x10) != 0;  // bit 4
    clk         = (raw & 0x20) != 0;  // bit 5
    cs          = (raw & 0x40) != 0;  // bit 6

    // EXROM is active-low on GMod2:
    // bit6 = 0 => EXROM active
    // bit6 = 1 => EXROM inactive
    exromHigh   = (raw & 0x40) != 0;

    writeEnable = (raw & 0x80) != 0;  // bit 7
}

void GMod2Mapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("GMD2");
    wrtr.writeU32(1); // version

    ctrl.save(wrtr);

    wrtr.writeU8(selectedBank);

    wrtr.endChunk();
}

bool GMod2Mapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "GMD2", 4) == 0)
    {
        uint32_t ver = 0;
        if (!rdr.readU32(ver))          { rdr.exitChunkPayload(chunk); return false; }
        if (ver != 1)                   { rdr.exitChunkPayload(chunk); return false; }

        if (!ctrl.load(rdr))            { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readU8(selectedBank))  { rdr.exitChunkPayload(chunk); return false; }

        if (!applyMappingAfterLoad())   { rdr.exitChunkPayload(chunk); return false; }

        rdr.exitChunkPayload(chunk);
        return true;
    }
    // Not our chunk
    return false;
}

uint8_t GMod2Mapper::read(uint16_t address)
{
    if (address == 0xDE00)
    {
        uint8_t result = ctrl.raw & 0x7F;

        if (eeprom.getDO())
            result |= 0x80;
        else
            result &= 0x7F;

        return result;
    }

    if (address >= 0x8000 && address <= 0x9FFF)
        return readFlashByte(address);

    return 0xFF;
}

void GMod2Mapper::write(uint16_t address, uint8_t value)
{
    if (address == 0xDE00)
    {
        ctrl.raw = value;
        ctrl.decode();
        applyMappingFromControl();
        return;
    }

    if (address >= 0x8000 && address <= 0x9FFF)
    {
        if (romWriteEnabled(address))
            handleFlashWrite(address, value);
        return;
    }
}

bool GMod2Mapper::loadIntoMemory(uint8_t bank)
{
    if (!mem || !cart)
        return false;

    if (!flashInitialized)
    {
        if (!rebuildFlashImageFromCRT())
            return false;
    }

    selectedBank = bank & 0x3F;

    cart->clearCartridge(cartLocation::LO);
    cart->clearCartridge(cartLocation::HI);
    cart->clearCartridge(cartLocation::HI_E000);

    const size_t base = static_cast<size_t>(selectedBank) * BANK_SIZE;

    if (base + BANK_SIZE > flashData.size())
        return false;

    for (size_t i = 0; i < BANK_SIZE; ++i)
        mem->writeCartridge(static_cast<uint16_t>(i), flashData[base + i], cartLocation::LO);

    return true;
}

bool GMod2Mapper::romWriteEnabled(uint16_t address) const
{
    return (address >= 0x8000 && address <= 0x9FFF) &&
           ctrl.writeEnable && ctrl.cs;
}

bool GMod2Mapper::applyMappingAfterLoad()
{
    if (!cart || !mem) return false;

    applyMappingFromControl();

    return true;
}

void GMod2Mapper::applyMappingFromControl()
{
    selectedBank = ctrl.romBank;

    cart->setExROMLine(ctrl.exromHigh);

    eeprom.setDI(ctrl.di);
    eeprom.setCS(ctrl.cs);
    eeprom.setCLK(ctrl.clk);

    (void)loadIntoMemory(selectedBank);
}

bool GMod2Mapper::rebuildFlashImageFromCRT()
{
    if (!cart)
        return false;

    flashData.assign(FLASH_SIZE, 0xFF);

    const auto& sections = cart->getChipSections();

    for (const auto& s : sections)
    {
        if (s.bankNumber >= BANK_COUNT)
            continue;

        if (s.data.size() < BANK_SIZE)
            continue;

        const size_t base = static_cast<size_t>(s.bankNumber) * BANK_SIZE;

        for (size_t i = 0; i < BANK_SIZE; ++i)
            flashData[base + i] = s.data[i];
    }

    flashInitialized = true;
    flashDirty = false;
    return true;
}

void GMod2Mapper::updateMappedByteIfVisible(uint8_t bank, uint16_t offset, uint8_t value)
{
    if (!mem)
        return;

    if ((bank & 0x3F) != selectedBank)
        return;

    if (offset >= BANK_SIZE)
        return;

    mem->writeCartridge(offset, value, cartLocation::LO);
}

void GMod2Mapper::resetFlashCommandState()
{
    flashCmdState = FlashCmdState::Idle;
}

uint8_t GMod2Mapper::readFlashByte(uint16_t address) const
{
    if (address < 0x8000 || address > 0x9FFF)
        return 0xFF;

    const uint16_t offset = static_cast<uint16_t>(address - 0x8000);

    if (flashReadMode == FlashReadMode::AutoSelect)
    {
        // Simple AMD-style autoselect values
        if (offset == 0x0000)
            return 0x01; // AMD manufacturer
        if (offset == 0x0001)
            return 0xA4; // placeholder device ID for now

        return 0xFF;
    }

    const size_t base = static_cast<size_t>(selectedBank) * BANK_SIZE;
    const size_t index = base + offset;

    if (index >= flashData.size())
        return 0xFF;

    return flashData[index];
}

void GMod2Mapper::programFlashByte(uint16_t address, uint8_t value)
{
    if (!flashInitialized)
    {
        if (!rebuildFlashImageFromCRT())
            return;
    }

    if (address < 0x8000 || address > 0x9FFF)
        return;

    const uint16_t offset = static_cast<uint16_t>(address - 0x8000);
    const size_t base = static_cast<size_t>(selectedBank) * BANK_SIZE;
    const size_t index = base + offset;

    if (index >= flashData.size())
        return;

    flashData[index] &= value;
    flashDirty = true;

    updateMappedByteIfVisible(selectedBank, offset, flashData[index]);
}

void GMod2Mapper::eraseFlashSector(uint16_t address)
{
    if (!flashInitialized)
    {
        if (!rebuildFlashImageFromCRT())
            return;
    }

    if (address < 0x8000 || address > 0x9FFF)
        return;

    const uint16_t offset = static_cast<uint16_t>(address - 0x8000);
    const size_t base = static_cast<size_t>(selectedBank) * BANK_SIZE;
    const size_t index = base + offset;

    if (index >= flashData.size())
        return;

    const size_t sectorBase = (index / SECTOR_SIZE) * SECTOR_SIZE;
    const size_t sectorEnd  = std::min(sectorBase + SECTOR_SIZE, flashData.size());

    for (size_t i = sectorBase; i < sectorEnd; ++i)
        flashData[i] = 0xFF;

    flashDirty = true;

    // Refresh visible bank if the active bank lies in this sector
    (void)loadIntoMemory(selectedBank);
}

void GMod2Mapper::eraseFlashChip()
{
    if (!flashInitialized)
    {
        if (!rebuildFlashImageFromCRT())
            return;
    }

    std::fill(flashData.begin(), flashData.end(), 0xFF);
    flashDirty = true;

    (void)loadIntoMemory(selectedBank);
}

void GMod2Mapper::handleFlashWrite(uint16_t address, uint8_t value)
{
    const uint16_t flashAddr = static_cast<uint16_t>(address - 0x8000);

    // Reset/read-array command
    if (value == 0xF0)
    {
        flashReadMode = FlashReadMode::ReadArray;
        resetFlashCommandState();
        return;
    }

    switch (flashCmdState)
    {
        case FlashCmdState::Idle:
        {
            if (flashAddr == 0x0555 && value == 0xAA)
            {
                flashCmdState = FlashCmdState::Unlock1Seen;
                return;
            }
            break;
        }

        case FlashCmdState::Unlock1Seen:
        {
            if (flashAddr == 0x02AA && value == 0x55)
            {
                flashCmdState = FlashCmdState::Unlock2Seen;
                return;
            }

            resetFlashCommandState();
            break;
        }

        case FlashCmdState::Unlock2Seen:
        {
            if (flashAddr == 0x0555 && value == 0xA0)
            {
                flashCmdState = FlashCmdState::ProgramSetup;
                return;
            }

            if (flashAddr == 0x0555 && value == 0x80)
            {
                flashCmdState = FlashCmdState::EraseSetup;
                return;
            }

            if (flashAddr == 0x0555 && value == 0x90)
            {
                flashReadMode = FlashReadMode::AutoSelect;
                resetFlashCommandState();
                return;
            }

            resetFlashCommandState();
            break;
        }

        case FlashCmdState::ProgramSetup:
        {
            programFlashByte(address, value);
            flashReadMode = FlashReadMode::ReadArray;
            resetFlashCommandState();
            return;
        }

        case FlashCmdState::EraseSetup:
        {
            if (flashAddr == 0x0555 && value == 0xAA)
            {
                flashCmdState = FlashCmdState::EraseUnlock1Seen;
                return;
            }

            resetFlashCommandState();
            break;
        }

        case FlashCmdState::EraseUnlock1Seen:
        {
            if (flashAddr == 0x02AA && value == 0x55)
            {
                flashCmdState = FlashCmdState::EraseUnlock2Seen;
                return;
            }

            resetFlashCommandState();
            break;
        }

        case FlashCmdState::EraseUnlock2Seen:
        {
            if (flashAddr == 0x0555 && value == 0x10)
            {
                eraseFlashChip();
                flashReadMode = FlashReadMode::ReadArray;
                resetFlashCommandState();
                return;
            }

            if (value == 0x30)
            {
                eraseFlashSector(address);
                flashReadMode = FlashReadMode::ReadArray;
                resetFlashCommandState();
                return;
            }

            resetFlashCommandState();
            break;
        }
    }

    // Any unexpected sequence drops back to normal
    flashReadMode = FlashReadMode::ReadArray;
    resetFlashCommandState();
}
