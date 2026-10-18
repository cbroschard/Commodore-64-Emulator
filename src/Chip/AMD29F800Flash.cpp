// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include <algorithm>
#include <cstring>
#include <memory>
#include <utility>
#include "Chip/AMD29F800Flash.h"

AMD29F800Flash::AMD29F800Flash() :
    state(FlashState::Read),
    pendingOperation(FlashOperation::None),
    eraseTimeoutRemaining(0),
    operationAddress(0),
    programmedValue(0),
    operationCyclesRemaining(0),
    toggleDQ2(false),
    toggleDQ6(false),
    operationFailed(false),
    dirty(false)
{
    flashData.fill(0xFF);
    selectedSectors.fill(false);
    sectorProtected.fill(false);
}

AMD29F800Flash::~AMD29F800Flash() = default;

void AMD29F800Flash::savePayload(StateWriter& wrtr) const
{
    for (uint8_t value : flashData)
        wrtr.writeU8(value);

    for (bool protectedSector : sectorProtected)
        wrtr.writeBool(protectedSector);

    wrtr.writeU8(static_cast<uint8_t>(state));
    wrtr.writeU8(static_cast<uint8_t>(pendingOperation));

    wrtr.writeU32(operationAddress);
    wrtr.writeU8(programmedValue);

    wrtr.writeU32(static_cast<uint32_t>(operationCyclesRemaining));
    wrtr.writeU32(static_cast<uint32_t>(operationCyclesRemaining >> 32));

    wrtr.writeU32(static_cast<uint32_t>(eraseTimeoutRemaining));
    wrtr.writeU32(static_cast<uint32_t>(eraseTimeoutRemaining >> 32));

    for (bool selected : selectedSectors)
        wrtr.writeBool(selected);

    wrtr.writeBool(toggleDQ2);
    wrtr.writeBool(toggleDQ6);
    wrtr.writeBool(operationFailed);
    wrtr.writeBool(dirty);
}

bool AMD29F800Flash::loadPayload(StateReader& rdr)
{
    auto newFlash = std::make_unique<std::array<uint8_t, FLASH_SIZE>>();

    for (auto& value : *newFlash)
    {
        if (!rdr.readU8(value))
            return false;
    }

    std::array<bool, SECTOR_COUNT> newProtection{};

    for (size_t i = 0; i < SECTOR_COUNT; ++i)
    {
        if (!rdr.readBool(newProtection[i]))
            return false;
    }

    uint8_t newState = 0;
    uint8_t newOperation = 0;

    if (!rdr.readU8(newState))
        return false;

    if (!rdr.readU8(newOperation))
        return false;

    if (newState > static_cast<uint8_t>(FlashState::EraseSuspended) || newOperation > static_cast<uint8_t>(FlashOperation::ChipErase))
        return false;

    uint32_t newAddress = 0;
    uint8_t newProgrammedValue = 0;

    uint32_t cycleLow = 0;
    uint32_t cycleHigh = 0;
    uint32_t timeoutLow = 0;
    uint32_t timeoutHigh = 0;

    if (!rdr.readU32(newAddress))
        return false;

    if (!rdr.readU8(newProgrammedValue))
        return false;

    if (!rdr.readU32(cycleLow))
        return false;

    if (!rdr.readU32(cycleHigh))
        return false;

    if (!rdr.readU32(timeoutLow))
        return false;

    if (!rdr.readU32(timeoutHigh))
        return false;

    if (newAddress >= FLASH_SIZE)
        return false;

    std::array<bool, SECTOR_COUNT> newSelected{};

    for (size_t i = 0; i < SECTOR_COUNT; ++i)
    {
        if (!rdr.readBool(newSelected[i]))
            return false;
    }

    bool newDQ2 = false;
    bool newDQ6 = false;
    bool newFailed = false;
    bool newDirty = false;

    if (!rdr.readBool(newDQ2) || !rdr.readBool(newDQ6) || !rdr.readBool(newFailed) || !rdr.readBool(newDirty))
        return false;

    // Commit only after all fields have been read successfully.
    flashData = std::move(*newFlash);
    sectorProtected = newProtection;
    selectedSectors = newSelected;

    state = static_cast<FlashState>(newState);
    pendingOperation = static_cast<FlashOperation>(newOperation);

    operationAddress = newAddress;
    programmedValue = newProgrammedValue;

    operationCyclesRemaining = (static_cast<uint64_t>(cycleHigh) << 32) | cycleLow;
    eraseTimeoutRemaining = (static_cast<uint64_t>(timeoutHigh) << 32) | timeoutLow;

    toggleDQ2 = newDQ2;
    toggleDQ6 = newDQ6;
    operationFailed = newFailed;
    dirty = newDirty;

    return true;
}

void AMD29F800Flash::reset()
{
    eraseTimeoutRemaining       = 0;

    state                       = FlashState::Read;
    pendingOperation            = FlashOperation::None;

    operationAddress            = 0;
    programmedValue             = 0;
    operationCyclesRemaining    = 0;

    toggleDQ2                   = false;
    toggleDQ6                   = false;
    operationFailed             = false;

    selectedSectors.fill(false);
}

void AMD29F800Flash::tick(uint64_t cycles)
{
    if (pendingOperation == FlashOperation::None)
        return;

    if (state == FlashState::EraseSuspended)
        return;

    // First, finish the sector-selection timeout.
    if (state == FlashState::EraseTimeout)
    {
        if (cycles < eraseTimeoutRemaining)
        {
            eraseTimeoutRemaining -= cycles;
            return;
        }

        cycles -= eraseTimeoutRemaining;
        beginSectorErase();
    }

    if (cycles < operationCyclesRemaining)
    {
        operationCyclesRemaining -= cycles;
        return;
    }

    operationCyclesRemaining = 0;

    switch (pendingOperation)
    {
        case FlashOperation::Program:
            programByte(operationAddress, programmedValue);
            break;

        case FlashOperation::SectorErase:
            completeSectorErase();
            break;

        case FlashOperation::ChipErase:
            eraseChip();
            break;

        case FlashOperation::None:
            break;
    }

    pendingOperation = FlashOperation::None;
    state = FlashState::Read;

    selectedSectors.fill(false);
    eraseTimeoutRemaining = 0;

    toggleDQ6 = false;
    toggleDQ2 = false;
    operationFailed = false;
}

uint8_t AMD29F800Flash::read(uint32_t address)
{
    if (address >= FLASH_SIZE)
        return 0xFF;

    switch (state)
    {
        case FlashState::Autoselect:
        {
            switch (address & 0xFF)
            {
                case 0x00:
                    return 0x01; // AMD manufacturer ID

                case 0x02:
                    return 0x58; // Provisional device ID

                case 0x04:
                {
                    const int sector = findSector(address);

                    if (sector < 0)
                        return 0xFF;

                    return sectorProtected[static_cast<size_t>(sector)] ? 0x01 : 0x00;
                }

            default:
                    return 0xFF;
            }
        }

        case FlashState::ProgramBusy:
        case FlashState::EraseBusy:
        case FlashState::EraseTimeout:
            return readBusyStatus(address);

        case FlashState::EraseSuspended:
        {
            if (isEraseTarget(address))
            {
                uint8_t result = flashData[address];

                // DQ2 continues toggling for suspended sectors.
                toggleDQ2 = !toggleDQ2;

                if (toggleDQ2)
                    result |= 0x04;
                else
                    result &= static_cast<uint8_t>(~0x04);

                // DQ6 must not toggle while suspended.
                result &= static_cast<uint8_t>(~0x40);

                // DQ7 reports suspend status.
                result |= 0x80;

                // DQ3 indicates the erase timeout has expired.
                result |= 0x08;

                return result;
            }

            // Other sectors can be read normally.
            return flashData[address];
        }

        case FlashState::Read:
        case FlashState::Unlock1:
        case FlashState::Unlock2:
        case FlashState::Program:
        case FlashState::EraseUnlock1:
        case FlashState::EraseUnlock2:
        case FlashState::EraseConfirm:
            return flashData[address];
    }

    return 0xFF;
}

void AMD29F800Flash::write(uint32_t address, uint8_t value)
{
    if (address >= FLASH_SIZE)
        return;

    const uint32_t commandAddress = address & 0x0FFF;

    constexpr uint32_t UNLOCK1 = 0x0AAA;
    constexpr uint32_t UNLOCK2 = 0x0555;

    // Suspend an active sector erase.
    if (state == FlashState::EraseBusy && pendingOperation == FlashOperation::SectorErase && value == 0xB0)
    {
        state = FlashState::EraseSuspended;
        return;
    }

    if (state == FlashState::EraseTimeout)
    {
        if (value == 0x30)
        {
            selectEraseSector(address);
            return;
        }

        if (value == 0xB0)
        {
            // Suspension during the selection window
            // terminates the window immediately.
            eraseTimeoutRemaining = 0;
            operationCyclesRemaining = SECTOR_ERASE_CYCLES;
            state = FlashState::EraseSuspended;
            return;
        }

        // Unsupported writes during the sector-selection window
        // do not discard the sectors already selected.
        return;
    }

    // Resume a suspended sector erase.
    if (state == FlashState::EraseSuspended && pendingOperation == FlashOperation::SectorErase && value == 0x30)
    {
        state = FlashState::EraseBusy;
        return;
    }

    // Program data may legitimately equal 0xF0.
    // Therefore, handle the Program state before reset commands.
    if (state == FlashState::Program)
    {
        startProgram(address, value);
        return;
    }

    // Basic software reset / exit autoselect.
    // Busy and suspended operations will need separate handling later.
    if (value == 0xF0 &&
        state != FlashState::ProgramBusy &&
        state != FlashState::EraseBusy &&
        state != FlashState::EraseSuspended)
    {
        state = FlashState::Read;
        return;
    }

    switch (state)
    {
        case FlashState::Read:
        {
            if (commandAddress == UNLOCK1 && value == 0xAA)
                state = FlashState::Unlock1;

            break;
        }

        case FlashState::Unlock1:
        {
            if (commandAddress == UNLOCK2 && value == 0x55)
                state = FlashState::Unlock2;
            else
                state = FlashState::Read;

            break;
        }

        case FlashState::Unlock2:
        {
            if (commandAddress != UNLOCK1)
            {
                state = FlashState::Read;
                break;
            }

            switch (value)
            {
                case 0xA0:
                    state = FlashState::Program;
                    break;

                case 0x80:
                    state = FlashState::EraseUnlock1;
                    break;

                case 0x90:
                    state = FlashState::Autoselect;
                    break;

                default:
                    state = FlashState::Read;
                    break;
            }

            break;
        }

        case FlashState::Autoselect:
        {
            // Exit through the F0 reset command handled above.
            break;
        }

        case FlashState::EraseUnlock1:
        {
            if (commandAddress == UNLOCK1 && value == 0xAA)
                state = FlashState::EraseUnlock2;
            else
                state = FlashState::Read;

            break;
        }

        case FlashState::EraseUnlock2:
        {
            if (commandAddress == UNLOCK2 && value == 0x55)
                state = FlashState::EraseConfirm;
            else
                state = FlashState::Read;

            break;
        }

        case FlashState::EraseConfirm:
        {
            if (commandAddress == UNLOCK1 && value == 0x10)
                startChipErase();
            else if (value == 0x30)
                startSectorEraseTimeout(address);
            else
                state = FlashState::Read;

            break;
        }

        case FlashState::EraseTimeout:
        case FlashState::Program:
        {
            // Already handled before the switch.
            break;
        }


        case FlashState::ProgramBusy:
        case FlashState::EraseBusy:
        case FlashState::EraseSuspended:
        {
            // Reserved for the timed operation implementation.
            break;
        }
    }
}

void AMD29F800Flash::loadImage(const uint8_t* data, size_t size)
{
    if (!data && size != 0)
        return;

    flashData.fill(0xFF);

    const size_t copySize = std::min(size, static_cast<size_t>(FLASH_SIZE));

    if (copySize != 0)
        std::copy_n(data, copySize, flashData.begin());

    reset();
    dirty = false;
}

bool AMD29F800Flash::isDirty() const
{
    return dirty;
}

void AMD29F800Flash::clearDirty()
{
    dirty = false;
}

void AMD29F800Flash::setSectorProtected(uint32_t sector, bool protectedState)
{
    if (sector >= SECTOR_COUNT)
        return;

    sectorProtected[sector] = protectedState;
}

bool AMD29F800Flash::isSectorProtected(uint32_t sector) const
{
    if (sector >= SECTOR_COUNT)
        return false;

    return sectorProtected[sector];
}

void AMD29F800Flash::programByte(uint32_t address, uint8_t value)
{
    if (address >= FLASH_SIZE)
        return;

    const int sector = findSector(address);

    if (sector < 0 || sectorProtected[static_cast<size_t>(sector)])
        return;

    const uint8_t oldValue = flashData[address];
    const uint8_t newValue = oldValue & value;

    if (newValue != oldValue)
    {
        flashData[address] = newValue;
        dirty = true;
    }
}

void AMD29F800Flash::eraseSector(uint32_t address)
{
    const int sector = findSector(address);

    if (sector < 0)
        return;

    if (sectorProtected[static_cast<size_t>(sector)])
        return;

    uint32_t start = 0;
    uint32_t size = 0;

    switch (sector)
    {
        case 0:
            start = 0x00000;
            size  = 0x4000;
            break;

        case 1:
            start = 0x04000;
            size  = 0x2000;
            break;

        case 2:
            start = 0x06000;
            size  = 0x2000;
            break;

        case 3:
            start = 0x08000;
            size  = 0x8000;
            break;

        default:
            start = 0x10000 + static_cast<uint32_t>(sector - 4) * 0x10000;
            size = 0x10000;
            break;
    }

    bool modified = false;

    for (uint32_t i = start; i < start + size; ++i)
    {
        if (flashData[i] != 0xFF)
        {
            flashData[i] = 0xFF;
            modified = true;
        }
    }

    if (modified)
        dirty = true;
}

void AMD29F800Flash::eraseChip()
{
    for (uint32_t sector = 0; sector < SECTOR_COUNT; ++sector)
    {
        if (sectorProtected[sector])
            continue;

        uint32_t address = 0;

        switch (sector)
        {
            case 0:
                address = 0x00000;
                break;

            case 1:
                address = 0x04000;
                break;

            case 2:
                address = 0x06000;
                break;

            case 3:
                address = 0x08000;
                break;

            default:
                address = 0x10000 + (sector - 4) * 0x10000;
                break;
        }

        eraseSector(address);
    }
}

int AMD29F800Flash::findSector(uint32_t address) const
{
    if (address >= FLASH_SIZE)
        return -1;

    if (address < 0x4000)
        return 0;

    if (address < 0x6000)
        return 1;

    if (address < 0x8000)
        return 2;

    if (address < 0x10000)
        return 3;

    return 4 + static_cast<int>((address - 0x10000) / 0x10000);
}

void AMD29F800Flash::startProgram(uint32_t address, uint8_t value)
{
    operationAddress = address;
    programmedValue = value;
    operationCyclesRemaining = PROGRAM_CYCLES;

    pendingOperation = FlashOperation::Program;
    state = FlashState::ProgramBusy;

    toggleDQ6 = false;
    toggleDQ2 = false;
    operationFailed = false;
}

void AMD29F800Flash::startChipErase()
{
    operationAddress = 0;
    operationCyclesRemaining = CHIP_ERASE_CYCLES;

    pendingOperation = FlashOperation::ChipErase;
    state = FlashState::EraseBusy;

    toggleDQ6 = false;
    toggleDQ2 = false;
    operationFailed = false;
}

uint8_t AMD29F800Flash::readBusyStatus(uint32_t address)
{
    uint8_t result = flashData[address];

    // Reads outside the active erase target should not
    // receive this operation's status response.
    if (pendingOperation == FlashOperation::SectorErase && state == FlashState::EraseBusy && !isEraseTarget(address))
        return result;

    // DQ6 toggles while programming or actively erasing.
    toggleDQ6 = !toggleDQ6;

    if (toggleDQ6)
        result |= 0x40;
    else
        result &= static_cast<uint8_t>(~0x40);

    if (pendingOperation == FlashOperation::Program)
    {
        // DQ7 is the inverse of the programmed data.
        if (programmedValue & 0x80)
            result &= static_cast<uint8_t>(~0x80);
        else
            result |= 0x80;

        // DQ2 does not toggle during ordinary programming.
        result &= static_cast<uint8_t>(~0x04);
        result &= static_cast<uint8_t>(~0x08);
    }
    else
    {
        // Erase in progress: DQ7 is low.
        result &= static_cast<uint8_t>(~0x80);

        if (isEraseTarget(address))
        {
            toggleDQ2 = !toggleDQ2;

            if (toggleDQ2)
                result |= 0x04;
            else
                result &= static_cast<uint8_t>(~0x04);
        }
        else
        {
            result &= static_cast<uint8_t>(~0x04);
        }

        // DQ3: Sector-erase timeout versus active erase.
        if (state == FlashState::EraseTimeout)
            result &= static_cast<uint8_t>(~0x08);
        else
            result |= 0x08;
    }

    // DQ5: Operation failure.
    if (operationFailed)
        result |= 0x20;
    else
        result &= static_cast<uint8_t>(~0x20);

    return result;
}

bool AMD29F800Flash::isEraseTarget(uint32_t address) const
{
    if (address >= FLASH_SIZE)
        return false;

    if (pendingOperation == FlashOperation::ChipErase)
        return true;

    if (pendingOperation == FlashOperation::SectorErase)
    {
        const int sector = findSector(address);

        return sector >= 0 &&
               selectedSectors[static_cast<size_t>(sector)];
    }

    return false;
}

void AMD29F800Flash::selectEraseSector(uint32_t address)
{
    const int sector = findSector(address);

    if (sector < 0)
        return;

    selectedSectors[static_cast<size_t>(sector)] = true;

    // Each additional valid sector selection
    // restarts the command acceptance window.
    eraseTimeoutRemaining = ERASE_TIMEOUT_CYCLES;
}

void AMD29F800Flash::startSectorEraseTimeout(uint32_t address)
{
    selectedSectors.fill(false);

    operationAddress = address;
    programmedValue = 0;

    operationCyclesRemaining = 0;

    pendingOperation = FlashOperation::SectorErase;
    state = FlashState::EraseTimeout;

    toggleDQ6 = false;
    toggleDQ2 = false;
    operationFailed = false;

    selectEraseSector(address);
}

void AMD29F800Flash::beginSectorErase()
{
    eraseTimeoutRemaining = 0;

    operationCyclesRemaining = SECTOR_ERASE_CYCLES;

    state = FlashState::EraseBusy;

    toggleDQ6 = false;
    toggleDQ2 = false;
}

void AMD29F800Flash::completeSectorErase()
{
    for (uint32_t sector = 0; sector < SECTOR_COUNT; ++sector)
    {
        if (!selectedSectors[sector])
            continue;

        uint32_t address = 0;

        switch (sector)
        {
            case 0:
                address = 0x00000;
                break;

            case 1:
                address = 0x04000;
                break;

            case 2:
                address = 0x06000;
                break;

            case 3:
                address = 0x08000;
                break;

            default:
                address = 0x10000 + (sector - 4) * 0x10000;
                break;
        }

        eraseSector(address);
    }

    selectedSectors.fill(false);
}
