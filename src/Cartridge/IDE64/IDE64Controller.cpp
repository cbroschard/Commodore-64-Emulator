// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge/IDE64/IDE64Controller.h"

IDE64Controller::IDE64Controller() :
    activeDevice(nullptr),
    selectedRegister(REG_RESERVED0),
    status(0x00),
    error(0x01),
    bufferIndex(0),
    bufferSize(0),
    currentLBA(0),
    sectorsRemaining(0),
    logicalCylinders(0),
    logicalHeads(16),
    logicalSectorsPerTrack(63),
    currentCylinder(0),
    currentHead(0)
{
    devices[0] = nullptr;
    devices[1] = nullptr;
    std::fill(sectorBuffer.begin(), sectorBuffer.end(), 0xff);
}

IDE64Controller::~IDE64Controller() = default;

void IDE64Controller::attachDevice(int index, IDE64BlockDevice* device)
{
    if (index < 0 || index > 1)
        return;

    devices[index] = device;
}

void IDE64Controller::reset()
{
    activeDevice            = nullptr;

    selectedRegister        = REG_RESERVED0;

    status                  = 0x00;
    error                   = 0x01;
    bufferIndex             = 0;
    bufferSize              = 0;
    currentLBA              = 0;
    sectorsRemaining        = 0;

    logicalCylinders        = 0;
    logicalHeads            = 16;
    logicalSectorsPerTrack  = 63;
    currentCylinder         = 0;
    currentHead             = 0;

    cmd                     = CurrentCommand::NONE;
    direction               = TransferDirection::NONE;

    registers.dataLo        = 0x00;
    registers.dataHi        = 0x00;

    for (int i = 0; i < 16; i++)
        registers.taskFile[i] = 0x00;

    registers.taskFile[REG_SECTOR_COUNT] = 0x01;
    registers.taskFile[REG_LBA0]         = 0x01;
    registers.taskFile[REG_LBA1]         = 0x00;
    registers.taskFile[REG_LBA2]         = 0x00;
    registers.taskFile[REG_DEVICE_HEAD]  = 0xA0;

    if (devices[0] && devices[0]->isPresent())
        status = 0x50;

    std::fill(sectorBuffer.begin(), sectorBuffer.end(), 0xFF);
}

void IDE64Controller::saveState(StateWriter& wrtr) const
{
    wrtr.writeU8(status);
    wrtr.writeU8(error);
    wrtr.writeU16(bufferIndex);
    wrtr.writeU16(bufferSize);
    wrtr.writeU32(currentLBA);
    wrtr.writeU16(sectorsRemaining);
    wrtr.writeU8(registers.dataLo);
    wrtr.writeU8(registers.dataHi);

    wrtr.writeU8(selectedRegister);

    wrtr.writeU8(static_cast<uint8_t>(cmd));
    wrtr.writeU8(static_cast<uint8_t>(direction));

    wrtr.writeU16(logicalCylinders);
    wrtr.writeU8(logicalHeads);
    wrtr.writeU8(logicalSectorsPerTrack);
    wrtr.writeU16(currentCylinder);
    wrtr.writeU8(currentHead);

    // Dump registers
    for (int i = 0; i < 16; i++)
        wrtr.writeU8(registers.taskFile[i]);

    // Dump Sector buffer
    for (size_t i = 0; i < sectorBuffer.size(); i++)
        wrtr.writeU8(sectorBuffer[i]);
}

bool IDE64Controller::loadState(StateReader& rdr)
{
    if (!rdr.readU8(status))                    return false;
    if (!rdr.readU8(error))                     return false;
    if (!rdr.readU16(bufferIndex))              return false;
    if (!rdr.readU16(bufferSize))               return false;
    if (!rdr.readU32(currentLBA))               return false;
    if (!rdr.readU16(sectorsRemaining))         return false;
    if (!rdr.readU8(registers.dataLo))          return false;
    if (!rdr.readU8(registers.dataHi))          return false;

    if (!rdr.readU8(selectedRegister))          return false;

    uint8_t tmpCmd = 0;
    if (!rdr.readU8(tmpCmd))                    return false;
    cmd = static_cast<CurrentCommand>(tmpCmd);

    uint8_t tmpDirection = 0;
    if (!rdr.readU8(tmpDirection))              return false;
    direction = static_cast<TransferDirection>(tmpDirection);

    if (!rdr.readU16(logicalCylinders))         return false;
    if (!rdr.readU8(logicalHeads))              return false;
    if (!rdr.readU8(logicalSectorsPerTrack))    return false;
    if (!rdr.readU16(currentCylinder))          return false;
    if (!rdr.readU8(currentHead))               return false;

    for (int i = 0; i < 16; i++)
        if (!rdr.readU8(registers.taskFile[i])) return false;

    for (size_t i = 0; i < sectorBuffer.size(); i++)
        if (!rdr.readU8(sectorBuffer[i]))       return false;

    // If the cmd is anything other than none, we need to ensure our device is active
    if (cmd != CurrentCommand::NONE)
    {
        activeDevice = getSelectedDevice();

        if (!activeDevice || !activeDevice->isPresent())
        {
            failCommand(0x04);
        }
    }
    else
    {
        activeDevice = nullptr;
    }

    return true;
}

uint8_t IDE64Controller::readRegister(uint16_t address)
{
    if (address >= TASKFILE_BASE && address <= TASKFILE_END)
    {
        const uint8_t reg = regIndex(address);
        selectedRegister = reg;

        if (reg == REG_RESERVED0)
        {
            if (direction == TransferDirection::TO_HOST &&
                bufferIndex + 1 < bufferSize)
            {
                registers.dataLo = sectorBuffer[bufferIndex];
                registers.dataHi = sectorBuffer[bufferIndex + 1];
            }

            return registers.dataLo;
        }

        uint8_t value = 0xFF;

        switch (reg)
        {
            case REG_ERROR:
                value = error;
                break;

            case REG_STATUS:
            case REG_ALT_STATUS:
                value = status;
                break;

            default:
                value = registers.taskFile[reg];
                break;
        }

        registers.dataLo = value;
        return value;
    }

    if (address == DATA_LO_ADDR)
        return registers.dataLo;

    if (address == DATA_HI_ADDR)
    {
        const uint8_t value = registers.dataHi;

        if (selectedRegister == REG_RESERVED0 &&
            direction == TransferDirection::TO_HOST &&
            bufferIndex + 1 < bufferSize)
        {
            bufferIndex += 2;

            if (bufferIndex >= bufferSize)
                handleReadBufferComplete();
        }

        return value;
    }

    return 0xFF;
}

void IDE64Controller::writeRegister(uint16_t address, uint8_t value)
{
    if (address >= TASKFILE_BASE && address <= TASKFILE_END)
    {
        const uint8_t reg = regIndex(address);

        selectedRegister = reg;

        // IDE64 stages the ATA low byte at $DE30 before accessing
        // the selected task-file register.
        const uint8_t registerValue = registers.dataLo;

        switch (reg)
        {
            case REG_RESERVED0:
            {
                // $DE20 commits one staged 16-bit ATA data word.
                if (direction == TransferDirection::FROM_HOST &&
                    cmd == CurrentCommand::WRITE_SECTORS &&
                    bufferIndex + 1 < bufferSize)
                {
                    sectorBuffer[bufferIndex] =
                        registers.dataLo;

                    sectorBuffer[bufferIndex + 1] =
                        registers.dataHi;

                    bufferIndex += 2;

                    if (bufferIndex >= bufferSize)
                        handleWriteBufferComplete();
                }

                break;
            }

            case REG_FEATURES:
                registers.taskFile[REG_FEATURES] =
                    registerValue;
                break;

            case REG_SECTOR_COUNT:
                registers.taskFile[REG_SECTOR_COUNT] =
                    registerValue;
                break;

            case REG_LBA0:
                registers.taskFile[REG_LBA0] =
                    registerValue;
                break;

            case REG_LBA1:
                registers.taskFile[REG_LBA1] =
                    registerValue;
                break;

            case REG_LBA2:
                registers.taskFile[REG_LBA2] =
                    registerValue;
                break;

            case REG_DEVICE_HEAD:
            {
                registers.taskFile[REG_DEVICE_HEAD] =
                    registerValue;

                activeDevice = getSelectedDevice();

                status =
                    (activeDevice && activeDevice->isPresent())
                        ? 0x50
                        : 0x00;

                activeDevice = nullptr;
                break;
            }

            case REG_COMMAND:
                registers.taskFile[REG_COMMAND] =
                    registerValue;

                executeCommand(registerValue);
                break;

            case REG_DEVICE_CTRL:
            {
                const uint8_t previous =
                    registers.taskFile[REG_DEVICE_CTRL];

                const bool resetWasAsserted =
                    (previous & 0x04) != 0;

                const bool resetIsAsserted =
                    (registerValue & 0x04) != 0;

                registers.taskFile[REG_DEVICE_CTRL] =
                    registerValue;

                if (resetIsAsserted)
                {
                    cmd = CurrentCommand::NONE;
                    direction = TransferDirection::NONE;
                    activeDevice = nullptr;

                    bufferIndex = 0;
                    bufferSize = 0;
                    currentLBA = 0;
                    sectorsRemaining = 0;

                    error = 0x00;
                    status = 0x80;
                }
                else if (resetWasAsserted)
                {
                    cmd = CurrentCommand::NONE;
                    direction = TransferDirection::NONE;

                    bufferIndex = 0;
                    bufferSize = 0;
                    currentLBA = 0;
                    sectorsRemaining = 0;

                    registers.dataLo = 0x00;
                    registers.dataHi = 0x00;

                    registers.taskFile[REG_SECTOR_COUNT] = 0x01;
                    registers.taskFile[REG_LBA0]         = 0x01;
                    registers.taskFile[REG_LBA1]         = 0x00;
                    registers.taskFile[REG_LBA2]         = 0x00;
                    registers.taskFile[REG_DEVICE_HEAD]  = 0xA0;
                    registers.taskFile[REG_COMMAND]      = 0x00;

                    error = 0x01;

                    activeDevice = getSelectedDevice();

                    status =
                        (activeDevice && activeDevice->isPresent())
                            ? 0x50
                            : 0x00;

                    activeDevice = nullptr;
                }

                break;
            }

            case REG_DRIVE_ADDR:
                registers.taskFile[REG_DRIVE_ADDR] =
                    registerValue;
                break;

            default:
                break;
        }

        return;
    }

    if (address == DATA_LO_ADDR)
    {
        // Stage the low ATA byte. Do not transfer it yet.
        registers.dataLo = value;
        return;
    }

    if (address == DATA_HI_ADDR)
    {
        // Stage the high ATA byte. The later $DE20 access commits it.
        registers.dataHi = value;
        return;
    }
}

void IDE64Controller::executeCommand(uint8_t value)
{
    switch (value)
    {
        case 0x10: // Recalibrate
        {
            currentCylinder = 0;
            currentHead = 0;

            finishCommandSuccess();
            return;
        }

        case 0x20: // READ SECTORS
        {
            activeDevice = getSelectedDevice();

            if (!activeDevice || !activeDevice->isPresent())
            {
                failCommand(0x04);
                return;
            }

            currentLBA = getCurrentLBA();

            if (currentLBA == UINT32_MAX)
            {
                failCommand(0x04);
                return;
            }

            sectorsRemaining = getNormalizedSectorCount();

            if (!activeDevice->readSector(
                    currentLBA,
                    sectorBuffer.data(),
                    SECTOR_SIZE))
            {
                failCommand(0x04);
                return;
            }

            --sectorsRemaining;

            cmd = CurrentCommand::READ_SECTORS;
            direction = TransferDirection::TO_HOST;
            bufferIndex = 0;
            bufferSize = SECTOR_SIZE;
            error = 0x00;
            status = 0x58; // DRDY + DSC + DRQ
            return;
        }

        case 0x30: // WRITE SECTORS
        {
            activeDevice = getSelectedDevice();

            if (!activeDevice || !activeDevice->isPresent())
            {
                failCommand(0x04);
                return;
            }

            if (activeDevice->isReadOnly())
            {
                failCommand(0x04);
                return;
            }

            currentLBA = getCurrentLBA();

            if (currentLBA == UINT32_MAX)
            {
                failCommand(0x04);
                return;
            }

            sectorsRemaining = getNormalizedSectorCount();

            cmd = CurrentCommand::WRITE_SECTORS;
            direction = TransferDirection::FROM_HOST;
            bufferIndex = 0;
            bufferSize = SECTOR_SIZE;
            error = 0x00;
            status = 0x58; // DRDY + DSC + DRQ

            std::fill(sectorBuffer.begin(), sectorBuffer.end(), 0x00);
            return;
        }

        case 0x40: // READ VERIFY SECTORS
        {
            activeDevice = getSelectedDevice();

            if (!activeDevice || !activeDevice->isPresent())
            {
                failCommand(0x04);
                return;
            }

            const uint32_t lba = getCurrentLBA();

            if (lba == UINT32_MAX)
            {
                failCommand(0x04);
                return;
            }

            const uint16_t count = getNormalizedSectorCount();

            if (lba >= activeDevice->sectorCount() ||
                count == 0 ||
                lba + count > activeDevice->sectorCount())
            {
                failCommand(0x04);
                return;
            }

            finishCommandSuccess();
            return;
        }

        case 0x70: // Seek
        {
            currentCylinder =
                static_cast<uint16_t>(registers.taskFile[REG_LBA1]) |
                static_cast<uint16_t>(
                    static_cast<uint16_t>(registers.taskFile[REG_LBA2]) << 8);

            currentHead = registers.taskFile[REG_DEVICE_HEAD] & 0x0F;

            finishCommandSuccess();
            return;
        }

        case 0x90: // EXECUTE DEVICE DIAGNOSTIC
        {
            activeDevice = getSelectedDevice();

            if (!activeDevice || !activeDevice->isPresent())
            {
                failCommand(0x04);
                return;
            }

            // ATA diagnostic code 0x01 commonly means device 0 passed.
            error = 0x01;
            finishCommandSuccess();
            return;
        }

        case 0x91: // INITIALIZE DEVICE PARAMETERS
        {
            activeDevice = getSelectedDevice();

            if (!activeDevice || !activeDevice->isPresent())
            {
                failCommand(0x04);
                return;
            }

            const uint8_t sectorsPerTrack =
                registers.taskFile[REG_SECTOR_COUNT];

            const uint8_t heads =
                static_cast<uint8_t>(
                    (registers.taskFile[REG_DEVICE_HEAD] & 0x0F) + 1);

            if (sectorsPerTrack == 0 || heads == 0)
            {
                failCommand(0x04);
                return;
            }

            logicalSectorsPerTrack = sectorsPerTrack;
            logicalHeads = heads;

            const uint32_t sectorsPerCylinder =
                static_cast<uint32_t>(logicalHeads) *
                logicalSectorsPerTrack;

            logicalCylinders =
                static_cast<uint16_t>(
                    activeDevice->sectorCount() / sectorsPerCylinder);

            if (logicalCylinders == 0)
                logicalCylinders = 1;

            finishCommandSuccess();
            return;
        }

        case 0xE1: // IDLE IMMEDIATE
        case 0xE3: // IDLE
        {
            activeDevice = getSelectedDevice();

            if (!activeDevice || !activeDevice->isPresent())
            {
                failCommand(0x04);
                return;
            }

            finishCommandSuccess();
            return;
        }

        case 0xE5: // CHECK POWER MODE
        {
            activeDevice = getSelectedDevice();

            if (!activeDevice || !activeDevice->isPresent())
            {
                failCommand(0x04);
                return;
            }

            // 0xFF generally means active/idle, not standby.
            registers.taskFile[REG_SECTOR_COUNT] = 0xFF;

            finishCommandSuccess();
            return;
        }

        case 0xE7: // FLUSH CACHE
        {
            activeDevice = getSelectedDevice();

            if (!activeDevice || !activeDevice->isPresent())
            {
                failCommand(0x04);
                return;
            }

            if (!activeDevice->flush())
            {
                failCommand(0x04);
                return;
            }

            finishCommandSuccess();
            return;
        }

        case 0xEC: // IDENTIFY DEVICE
        {
            activeDevice = getSelectedDevice();

            if (!activeDevice || !activeDevice->isPresent())
            {
                failCommand(0x04);
                return;
            }

            cmd = CurrentCommand::IDENTIFY_DEVICE;
            direction = TransferDirection::TO_HOST;
            error = 0x00;

            IDE64BlockDevice::DeviceInfo info =
                activeDevice->getDeviceInfo();

            prepareIdentifyData(info);

            status = 0x58; // DRDY + DSC + DRQ
            bufferIndex = 0;
            bufferSize = 512;
            sectorsRemaining = 0;
            return;
        }

        case 0xEF: // SET FEATURES
        {
            activeDevice = getSelectedDevice();

            if (!activeDevice || !activeDevice->isPresent())
            {
                failCommand(0x04);
                return;
            }

            // For now, accept feature changes as a compatibility no-op.
            finishCommandSuccess();
            return;
        }

        default:
            failCommand(0x04);
            return;
    }
}

void IDE64Controller::prepareIdentifyData(const IDE64BlockDevice::DeviceInfo& info)
{
    std::fill(sectorBuffer.begin(), sectorBuffer.end(), 0x00);

    setIdentifyWord(0, 0x0040);
    setIdentifyWord(1, info.cylinders);
    setIdentifyWord(3, info.heads);
    setIdentifyWord(6, info.sectorsPerTrack);

    setIdentifyString(10, 10, info.serialNumber);
    setIdentifyString(23, 4,  info.firmwareRevision);
    setIdentifyString(27, 20, info.modelNumber);

    setIdentifyWord(49, 0x0200);
    setIdentifyWord(53, 0x0001);

    setIdentifyWord(60, info.totalSectors & 0xFFFF);
    setIdentifyWord(61, (info.totalSectors >> 16) & 0xFFFF);
}

void IDE64Controller::setIdentifyWord(uint8_t index, uint16_t value)
{
    size_t offset = static_cast<size_t>(index) * 2;
    if (offset + 1 >= sectorBuffer.size()) return;

    sectorBuffer[offset]     = static_cast<uint8_t>(value & 0x00FF);
    sectorBuffer[offset + 1] = static_cast<uint8_t>((value >> 8) & 0x00FF);
}

void IDE64Controller::setIdentifyString(uint8_t startIndex, uint8_t wordCount, const std::string& text)
{
    const size_t fieldLength = static_cast<size_t>(wordCount) * 2;
    std::string field(fieldLength, ' ');

    const size_t copyLength = std::min(text.size(), fieldLength);
    for (size_t i = 0; i < copyLength; i++)
        field[i] = text[i];

    for (uint8_t i = 0; i < wordCount; i++)
    {
        uint8_t c0 = static_cast<uint8_t>(field[static_cast<size_t>(i) * 2]);
        uint8_t c1 = static_cast<uint8_t>(field[static_cast<size_t>(i) * 2 + 1]);

        uint16_t word = (static_cast<uint16_t>(c0) << 8) |
                        static_cast<uint16_t>(c1);

        setIdentifyWord(startIndex + i, word);
    }
}

void IDE64Controller::finishCommandSuccess()
{
    activeDevice        = nullptr;
    cmd                 = CurrentCommand::NONE;
    currentLBA          = 0;
    direction           = TransferDirection::NONE;
    bufferIndex         = 0;
    bufferSize          = 0;
    sectorsRemaining    = 0;
    error               = 0x00;
    status              = 0x50;
}

void IDE64Controller::failCommand(uint8_t errorCode)
{
    activeDevice        = nullptr;
    cmd                 = CurrentCommand::NONE;
    currentLBA          = 0;
    direction           = TransferDirection::NONE;
    bufferIndex         = 0;
    bufferSize          = 0;
    sectorsRemaining    = 0;
    error               = errorCode;
    status              = 0x51;
}

uint32_t IDE64Controller::getCurrentLBA() const
{
    const uint8_t sectorNumber =
        registers.taskFile[REG_LBA0];

    const uint8_t cylinderLow =
        registers.taskFile[REG_LBA1];

    const uint8_t cylinderHigh =
        registers.taskFile[REG_LBA2];

    const uint8_t deviceHead =
        registers.taskFile[REG_DEVICE_HEAD];

    const bool lbaMode =
        (deviceHead & 0x40) != 0;

    if (lbaMode)
    {
        const uint8_t lba3 =
            deviceHead & 0x0F;

        return static_cast<uint32_t>(sectorNumber) |
               (static_cast<uint32_t>(cylinderLow) << 8) |
               (static_cast<uint32_t>(cylinderHigh) << 16) |
               (static_cast<uint32_t>(lba3) << 24);
    }

    // CHS addressing uses a one-based sector number.
    const uint16_t cylinder =
        static_cast<uint16_t>(cylinderLow) |
        static_cast<uint16_t>(
            static_cast<uint16_t>(cylinderHigh) << 8);

    const uint8_t head =
        deviceHead & 0x0F;

    if (sectorNumber == 0 ||
        logicalHeads == 0 ||
        logicalSectorsPerTrack == 0 ||
        head >= logicalHeads ||
        sectorNumber > logicalSectorsPerTrack)
    {
        return UINT32_MAX;
    }

    return
        ((static_cast<uint32_t>(cylinder) * logicalHeads) + head) *
        logicalSectorsPerTrack +
        (sectorNumber - 1);
}

uint16_t IDE64Controller::getNormalizedSectorCount() const
{
    const uint8_t raw = registers.taskFile[REG_SECTOR_COUNT];

    return (raw == 0) ? 256 : raw;
}

void IDE64Controller::handleReadBufferComplete()
{
    if (cmd == CurrentCommand::READ_SECTORS && sectorsRemaining > 0)
    {
        ++currentLBA;

        if (!activeDevice ||
            !activeDevice->isPresent() ||
            !activeDevice->readSector(currentLBA, sectorBuffer.data(), SECTOR_SIZE))
        {
            failCommand(0x04);
            return;
        }

        --sectorsRemaining;
        bufferIndex = 0;
        bufferSize = SECTOR_SIZE;
        status = 0x58;
        error = 0x00;
        return;
    }

    finishCommandSuccess();
}

void IDE64Controller::handleWriteBufferComplete()
{
    if (cmd != CurrentCommand::WRITE_SECTORS)
    {
        finishCommandSuccess();
        return;
    }

    if (!activeDevice ||
        !activeDevice->isPresent() ||
        activeDevice->isReadOnly() ||
        !activeDevice->writeSector(currentLBA, sectorBuffer.data(), SECTOR_SIZE))
    {
        failCommand(0x04);
        return;
    }

    if (sectorsRemaining > 0)
        --sectorsRemaining;

    if (sectorsRemaining > 0)
    {
        ++currentLBA;
        bufferIndex = 0;
        bufferSize = SECTOR_SIZE;
        status = 0x58;
        error = 0x00;
        std::fill(sectorBuffer.begin(), sectorBuffer.end(), 0x00);
        return;
    }

    finishCommandSuccess();
}
