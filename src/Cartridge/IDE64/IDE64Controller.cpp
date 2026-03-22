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
    status(0x40),
    error(0x00),
    bufferIndex(0),
    bufferSize(0),
    currentLBA(0),
    sectorsRemaining(0)
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
    activeDevice        = nullptr;

    status              = 0x40;
    error               = 0x00;
    bufferIndex         = 0;
    bufferSize          = 0;
    currentLBA          = 0;
    sectorsRemaining    = 0;

    cmd                 = CurrentCommand::NONE;
    direction           = TransferDirection::NONE;

    registers.dataLo    = 0x00;
    registers.dataHi    = 0x00;

    for (int i = 0; i < 16; i++)
        registers.taskFile[i] = 0x00;

    std::fill(sectorBuffer.begin(), sectorBuffer.end(), 0xFF);
}

void IDE64Controller::saveState(StateWriter& wrtr) const
{
    wrtr.writeU8(status);
    wrtr.writeU8(error);
    wrtr.writeU16(bufferIndex);
    wrtr.writeU16(bufferSize);
    wrtr.writeU32(currentLBA);
    wrtr.writeU8(sectorsRemaining);
    wrtr.writeU8(registers.dataLo);
    wrtr.writeU8(registers.dataHi);
    wrtr.writeU8(static_cast<uint8_t>(cmd));
    wrtr.writeU8(static_cast<uint8_t>(direction));

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
    if (!rdr.readU8(sectorsRemaining))          return false;
    if (!rdr.readU8(registers.dataLo))          return false;
    if (!rdr.readU8(registers.dataHi))          return false;

    uint8_t tmpCmd = 0;
    if (!rdr.readU8(tmpCmd))                    return false;
    cmd = static_cast<CurrentCommand>(tmpCmd);

    uint8_t tmpDirection = 0;
    if (!rdr.readU8(tmpDirection))              return false;
    direction = static_cast<TransferDirection>(tmpDirection);

    for (int i = 0; i < 16; i++)
        if (!rdr.readU8(registers.taskFile[i])) return false;

    for (size_t i = 0; i < sectorBuffer.size(); i++)
        if (!rdr.readU8(sectorBuffer[i]))       return false;

    return true;
}

uint8_t IDE64Controller::readRegister(uint16_t address)
{
    if (address >= TASKFILE_BASE && address <= TASKFILE_END)
    {
        const uint8_t reg = regIndex(address);

        switch (reg)
        {
            case REG_ERROR:
                return error;

            case REG_STATUS:
            case REG_ALT_STATUS:
                return status;

            default:
                return registers.taskFile[reg];
        }
    }

   if (address == DATA_LO_ADDR)
    {
        if (direction == TransferDirection::TO_HOST && bufferIndex + 1 < bufferSize)
            return sectorBuffer[bufferIndex];

        return registers.dataLo;
    }

    if (address == DATA_HI_ADDR)
    {
        if (direction == TransferDirection::TO_HOST && bufferIndex + 1 < bufferSize)
        {
            uint8_t value = sectorBuffer[bufferIndex + 1];
            bufferIndex += 2;

            if (bufferIndex >= bufferSize)
            {
                // End of read reset status
                finishCommandSuccess();
            }
            return value;
        }

        return registers.dataHi;
    }

    return 0xFF;
}

void IDE64Controller::writeRegister(uint16_t address, uint8_t value)
{
    if (address >= TASKFILE_BASE && address <= TASKFILE_END)
    {
        const uint8_t reg = regIndex(address);
        registers.taskFile[reg] = value;

        switch (reg)
        {
            case REG_FEATURES:
                break;

            case REG_SECTOR_COUNT:
                break;

            case REG_LBA0:
                break;

            case REG_LBA1:
                break;

            case REG_LBA2:
                break;

            case REG_DEVICE_HEAD:
                break;

            case REG_COMMAND:
                executeCommand(value);
                break;

            case REG_DEVICE_CTRL:
                break;

            case REG_DRIVE_ADDR:
                break;

            default:
                break;
        }
        return;
    }
    else if (address == DATA_LO_ADDR)
    {

    }
    else if (address == DATA_HI_ADDR)
    {

    }
}

void IDE64Controller::executeCommand(uint8_t value)
{
    switch (value)
    {
        case 0xEC:  // Identify Device
        {
            activeDevice            = getSelectedDevice();
            if (!activeDevice || !activeDevice->isPresent())
            {
                failCommand(0x04);
                return;
            }

            cmd                     = CurrentCommand::IDENTIFY_DEVICE;
            direction               = TransferDirection::TO_HOST;
            error                   = 0x00;

            // Get the selected devices info and pass over for identify
            IDE64BlockDevice::DeviceInfo info = activeDevice->getDeviceInfo();
            prepareIdentifyData(info);

            status                  = 0x48;
            bufferIndex             = 0;
            bufferSize              = 512;
            sectorsRemaining        = 0;
            break;
        }
        case 0x20:  // Read sectors
        case 0x30:  // Write ssctors
        default:
            break;
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
    activeDevice = nullptr;
    cmd = CurrentCommand::NONE;
    direction = TransferDirection::NONE;
    bufferIndex = 0;
    bufferSize = 0;
    sectorsRemaining = 0;
    error = 0x00;
    status = 0x40;
}

void IDE64Controller::failCommand(uint8_t errorCode)
{
    activeDevice = nullptr;
    cmd = CurrentCommand::NONE;
    direction = TransferDirection::NONE;
    bufferIndex = 0;
    bufferSize = 0;
    sectorsRemaining = 0;
    error = errorCode;
    status = 0x41;
}
