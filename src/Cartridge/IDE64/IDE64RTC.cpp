// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include <fstream>
#include <string>
#include "Cartridge/IDE64/IDE64RTC.h"

IDE64RTC::IDE64RTC()
{
    cmosRAM.fill(0xFF);
    reset();
}

IDE64RTC::~IDE64RTC() = default;

void IDE64RTC::reset()
{
    wireState.chipEnabled           = false;
    wireState.phase                 = TransferPhase::Command;
    wireState.shiftRegister         = 0;
    wireState.outputShiftRegister   = 0;
    wireState.bitCount              = 0;
    wireState.transferIndex         = 0;
    wireState.command               = 0;
    wireState.address               = 0;
    wireState.ramSelected           = false;
    wireState.readOperation         = false;
    wireState.burstOperation        = false;
    wireState.dataOut               = true;
}

void IDE64RTC::setChipEnabled(bool enabled)
{
    if (enabled == wireState.chipEnabled)
        return;

    if (enabled)
    {
        // Start a new transaction.
        wireState.chipEnabled           = true;
        wireState.phase                 = TransferPhase::Command;
        wireState.shiftRegister         = 0;
        wireState.outputShiftRegister   = 0;
        wireState.bitCount              = 0;
        wireState.transferIndex         = 0;
        wireState.command               = 0;
        wireState.address               = 0;
        wireState.ramSelected           = false;
        wireState.readOperation         = false;
        wireState.burstOperation        = false;
        wireState.dataOut               = true;
    }
    else
    {
        // End the current transaction.
        wireState.chipEnabled           = false;
        wireState.phase                 = TransferPhase::Command;
        wireState.shiftRegister         = 0;
        wireState.outputShiftRegister   = 0;
        wireState.bitCount              = 0;
        wireState.transferIndex         = 0;
        wireState.command               = 0;
        wireState.address               = 0;
        wireState.ramSelected           = false;
        wireState.readOperation         = false;
        wireState.burstOperation        = false;

        // Released serial line reads high.
        wireState.dataOut               = true;
    }
}

void IDE64RTC::saveState(StateWriter& wrtr) const
{
    // Clock/calendar state
    wrtr.writeU8(rtcState.seconds);
    wrtr.writeU8(rtcState.minutes);
    wrtr.writeU8(rtcState.hours);
    wrtr.writeU8(rtcState.dayOfWeek);
    wrtr.writeU8(rtcState.dayOfMonth);
    wrtr.writeU8(rtcState.month);
    wrtr.writeU16(rtcState.year);
    wrtr.writeU8(rtcState.writeProtect);
    wrtr.writeU8(rtcState.trickleCharger);
    wrtr.writeBool(rtcState.clockHalted);
    wrtr.writeBool(rtcState.hourMode12);
    wrtr.writeBool(rtcState.hourPM);

    // Current serial transaction state
    wrtr.writeBool(wireState.chipEnabled);
    wrtr.writeU8(static_cast<uint8_t>(wireState.phase));
    wrtr.writeU8(wireState.shiftRegister);
    wrtr.writeU8(wireState.outputShiftRegister);
    wrtr.writeU8(wireState.bitCount);
    wrtr.writeU8(wireState.transferIndex);
    wrtr.writeU8(wireState.command);
    wrtr.writeU8(wireState.address);
    wrtr.writeBool(wireState.ramSelected);
    wrtr.writeBool(wireState.readOperation);
    wrtr.writeBool(wireState.burstOperation);
    wrtr.writeBool(wireState.dataOut);

    // Battery-backed CMOS RAM
    for (uint8_t value : cmosRAM)
        wrtr.writeU8(value);
}

bool IDE64RTC::loadState(StateReader& rdr)
{
    // Clock/calendar state
    if (!rdr.readU8(rtcState.seconds))                          return false;
    if (!rdr.readU8(rtcState.minutes))                          return false;
    if (!rdr.readU8(rtcState.hours))                            return false;
    if (!rdr.readU8(rtcState.dayOfWeek))                        return false;
    if (!rdr.readU8(rtcState.dayOfMonth))                       return false;
    if (!rdr.readU8(rtcState.month))                            return false;
    if (!rdr.readU16(rtcState.year))                            return false;
    if (!rdr.readU8(rtcState.writeProtect))                     return false;
    if (!rdr.readU8(rtcState.trickleCharger))                   return false;
    if (!rdr.readBool(rtcState.clockHalted))                    return false;
    if (!rdr.readBool(rtcState.hourMode12))                     return false;
    if (!rdr.readBool(rtcState.hourPM))                         return false;

    // Current serial transaction state
    if (!rdr.readBool(wireState.chipEnabled))                   return false;

    uint8_t phase = 0;

    if (!rdr.readU8(phase))                                     return false;
    if (phase > static_cast<uint8_t>(TransferPhase::Ignore))    return false;

    wireState.phase =
        static_cast<TransferPhase>(phase);

    if (!rdr.readU8(wireState.shiftRegister))                   return false;
    if (!rdr.readU8(wireState.outputShiftRegister))             return false;
    if (!rdr.readU8(wireState.bitCount))                        return false;
    if (!rdr.readU8(wireState.transferIndex))                   return false;
    if (!rdr.readU8(wireState.command))                         return false;
    if (!rdr.readU8(wireState.address))                         return false;
    if (!rdr.readBool(wireState.ramSelected))                   return false;
    if (!rdr.readBool(wireState.readOperation))                 return false;
    if (!rdr.readBool(wireState.burstOperation))                return false;
    if (!rdr.readBool(wireState.dataOut))                       return false;

    // Battery-backed CMOS RAM
    for (uint8_t& value : cmosRAM)
    {
        if (!rdr.readU8(value))
            return false;
    }

    return true;
}

bool IDE64RTC::savePersistence(const std::string& path) const
{
    std::ofstream file(
        path,
        std::ios::binary | std::ios::trunc);

    if (!file.is_open())
        return false;

    const char magic[8] =
    {
        'I', 'D', 'E', '6', '4', 'R', 'T', 'C'
    };

    file.write(magic, sizeof(magic));

    auto writeU8 = [&file](uint8_t value)
    {
        file.put(static_cast<char>(value));
    };

    writeU8(1); // Persistence format version

    writeU8(rtcState.seconds);
    writeU8(rtcState.minutes);
    writeU8(rtcState.hours);
    writeU8(rtcState.dayOfWeek);
    writeU8(rtcState.dayOfMonth);
    writeU8(rtcState.month);

    writeU8(static_cast<uint8_t>(rtcState.year & 0xFF));
    writeU8(static_cast<uint8_t>((rtcState.year >> 8) & 0xFF));

    writeU8(rtcState.writeProtect);
    writeU8(rtcState.trickleCharger);
    writeU8(rtcState.clockHalted ? 1 : 0);
    writeU8(rtcState.hourMode12 ? 1 : 0);
    writeU8(rtcState.hourPM ? 1 : 0);

    file.write(
        reinterpret_cast<const char*>(cmosRAM.data()),
        static_cast<std::streamsize>(cmosRAM.size()));

    return static_cast<bool>(file);
}

bool IDE64RTC::loadPersistence(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);

    // No persistence file yet is a valid first-run condition.
    if (!file.is_open())
        return true;

    char magic[8] = {};

    if (!file.read(magic, sizeof(magic)))
        return false;

    const char expectedMagic[8] =
    {
        'I', 'D', 'E', '6', '4', 'R', 'T', 'C'
    };

    if (std::memcmp(
            magic,
            expectedMagic,
            sizeof(expectedMagic)) != 0)
    {
        return false;
    }

    auto readU8 = [&file](uint8_t& value) -> bool
    {
        char byte = 0;

        if (!file.get(byte))
            return false;

        value = static_cast<uint8_t>(
            static_cast<unsigned char>(byte));

        return true;
    };

    uint8_t version = 0;

    if (!readU8(version))
        return false;

    if (version != 1)
        return false;

    RTCState loadedState = rtcState;
    decltype(cmosRAM) loadedCMOS{};

    if (!readU8(loadedState.seconds))         return false;
    if (!readU8(loadedState.minutes))         return false;
    if (!readU8(loadedState.hours))           return false;
    if (!readU8(loadedState.dayOfWeek))       return false;
    if (!readU8(loadedState.dayOfMonth))      return false;
    if (!readU8(loadedState.month))           return false;

    uint8_t yearLow = 0;
    uint8_t yearHigh = 0;

    if (!readU8(yearLow))                     return false;
    if (!readU8(yearHigh))                    return false;

    loadedState.year =
        static_cast<uint16_t>(yearLow) |
        static_cast<uint16_t>(
            static_cast<uint16_t>(yearHigh) << 8);

    if (!readU8(loadedState.writeProtect))    return false;
    if (!readU8(loadedState.trickleCharger))  return false;

    uint8_t clockHalted = 0;
    uint8_t hourMode12 = 0;
    uint8_t hourPM = 0;

    if (!readU8(clockHalted))                 return false;
    if (!readU8(hourMode12))                  return false;
    if (!readU8(hourPM))                      return false;

    if (clockHalted > 1 ||
        hourMode12 > 1 ||
        hourPM > 1)
    {
        return false;
    }

    loadedState.clockHalted = clockHalted != 0;
    loadedState.hourMode12 = hourMode12 != 0;
    loadedState.hourPM = hourPM != 0;

    if (!file.read(
            reinterpret_cast<char*>(loadedCMOS.data()),
            static_cast<std::streamsize>(loadedCMOS.size())))
    {
        return false;
    }

    rtcState = loadedState;
    cmosRAM = loadedCMOS;

    // Persistence never restores a partially completed wire transfer.
    reset();

    return true;
}

uint8_t IDE64RTC::readByte()
{
    if (!wireState.chipEnabled)
        return 0xFF;

    if (wireState.phase != TransferPhase::ReadData)
        return 0xFF;

    const uint8_t bit =
        wireState.outputShiftRegister & 0x01;

    wireState.outputShiftRegister >>= 1;
    ++wireState.bitCount;

    if (wireState.bitCount == 8)
    {
        wireState.bitCount = 0;

        if (wireState.burstOperation)
        {
            ++wireState.transferIndex;

            if (wireState.ramSelected)
            {
                if (wireState.transferIndex < CMOS_RAM_SIZE)
                {
                    wireState.outputShiftRegister =
                        cmosRAM[wireState.transferIndex];
                }
                else
                {
                    wireState.phase = TransferPhase::Ignore;
                    wireState.outputShiftRegister = 0xFF;
                }
            }
            else
            {
                // Clock burst contains registers 0 through 7.
                if (wireState.transferIndex < 8)
                {
                    wireState.outputShiftRegister =
                        readClockRegister(wireState.transferIndex);
                }
                else
                {
                    wireState.phase = TransferPhase::Ignore;
                    wireState.outputShiftRegister = 0xFF;
                }
            }
        }
        else
        {
            wireState.phase = TransferPhase::Ignore;
        }
    }

    wireState.dataOut = bit != 0;

    return static_cast<uint8_t>(0xFE | bit);
}

void IDE64RTC::writeByte(uint8_t value)
{
    if (!wireState.chipEnabled)
        return;

    const uint8_t bit = value & 0x01;

    if (wireState.phase == TransferPhase::Command)
    {
        wireState.shiftRegister |=
            static_cast<uint8_t>(bit << wireState.bitCount);

        ++wireState.bitCount;

        if (wireState.bitCount == 8)
            decodeCommand(wireState.shiftRegister);

        return;
    }

    if (wireState.phase == TransferPhase::WriteData)
    {
        wireState.shiftRegister |=
            static_cast<uint8_t>(bit << wireState.bitCount);

        ++wireState.bitCount;

        if (wireState.bitCount == 8)
        {
            if (wireState.ramSelected)
            {
                const bool writesEnabled =
                    (rtcState.writeProtect & 0x80) == 0;

                if (wireState.burstOperation)
                {
                    if (wireState.transferIndex < CMOS_RAM_SIZE)
                    {
                        if (writesEnabled)
                        {
                            cmosRAM[wireState.transferIndex] =
                                wireState.shiftRegister;
                        }

                        ++wireState.transferIndex;
                    }

                    if (wireState.transferIndex >= CMOS_RAM_SIZE)
                        wireState.phase = TransferPhase::Ignore;
                }
                else
                {
                    if (writesEnabled &&
                        wireState.address < CMOS_RAM_SIZE)
                    {
                        cmosRAM[wireState.address] =
                            wireState.shiftRegister;
                    }

                    wireState.phase = TransferPhase::Ignore;
                }
            }
            else
            {
                if (wireState.burstOperation)
                {
                    // Clock burst writes registers 0 through 7.
                    if (wireState.transferIndex < 8)
                    {
                        writeClockRegister(
                            wireState.transferIndex,
                            wireState.shiftRegister);

                        ++wireState.transferIndex;
                    }

                    if (wireState.transferIndex >= 8)
                        wireState.phase = TransferPhase::Ignore;
                }
                else
                {
                    if (wireState.address <= 8)
                    {
                        writeClockRegister(
                            wireState.address,
                            wireState.shiftRegister);
                    }

                    wireState.phase = TransferPhase::Ignore;
                }
            }

            wireState.shiftRegister = 0;
            wireState.bitCount = 0;
        }

        return;
    }
}

void IDE64RTC::decodeCommand(uint8_t command)
{
    wireState.command = command;

    if ((command & 0x80) == 0)
    {
        wireState.phase = TransferPhase::Ignore;
        return;
    }

    wireState.ramSelected =
        (command & 0x40) != 0;

    wireState.address =
        static_cast<uint8_t>((command >> 1) & 0x1F);

    wireState.readOperation =
        (command & 0x01) != 0;

    wireState.burstOperation =
        wireState.address == 0x1F;

    wireState.shiftRegister = 0;
    wireState.outputShiftRegister = 0;
    wireState.bitCount = 0;
    wireState.transferIndex = 0;

    if (wireState.readOperation)
    {
        wireState.phase = TransferPhase::ReadData;

        if (wireState.ramSelected)
        {
            if (wireState.burstOperation)
            {
                // $FF starts reading CMOS RAM at byte zero.
                wireState.outputShiftRegister =
                    cmosRAM[wireState.transferIndex];
            }
            else if (wireState.address < CMOS_RAM_SIZE)
            {
                wireState.outputShiftRegister =
                    cmosRAM[wireState.address];
            }
            else
            {
                wireState.outputShiftRegister = 0xFF;
                wireState.phase = TransferPhase::Ignore;
            }
        }
        else
        {
            if (wireState.burstOperation)
            {
                // $BF starts reading clock register zero.
                wireState.outputShiftRegister =
                    readClockRegister(wireState.transferIndex);
            }
            else if (wireState.address <= 8)
            {
                wireState.outputShiftRegister =
                    readClockRegister(wireState.address);
            }
            else
            {
                wireState.outputShiftRegister = 0xFF;
                wireState.phase = TransferPhase::Ignore;
            }
        }
    }
    else
    {
        // The following serial bits contain write data.
        wireState.phase = TransferPhase::WriteData;
    }
}

uint8_t IDE64RTC::readClockRegister(uint8_t address) const
{
    switch (address)
    {
        case 0:
            return static_cast<uint8_t>(
                binaryToBCD(rtcState.seconds) |
                (rtcState.clockHalted ? 0x80 : 0x00));

        case 1:
            return binaryToBCD(rtcState.minutes);

        case 2:
        {
            if (rtcState.hourMode12)
            {
                return static_cast<uint8_t>(
                    0x80 |
                    (rtcState.hourPM ? 0x20 : 0x00) |
                    (binaryToBCD(rtcState.hours) & 0x1F));
            }

            return static_cast<uint8_t>(
                binaryToBCD(rtcState.hours) & 0x3F);
        }

        case 3:
            return binaryToBCD(rtcState.dayOfMonth);

        case 4:
            return binaryToBCD(rtcState.month);

        case 5:
            return binaryToBCD(rtcState.dayOfWeek);

        case 6:
            return binaryToBCD(
                static_cast<uint8_t>(rtcState.year % 100));

        case 7:
            return rtcState.writeProtect;

        case 8:
            return rtcState.trickleCharger;

        default:
            return 0xFF;
    }
}

void IDE64RTC::writeClockRegister(uint8_t address, uint8_t value)
{
    // The write-protect register must remain writable so protection
    // can be disabled.
    if (address == 7)
    {
        rtcState.writeProtect =
            static_cast<uint8_t>(value & 0x80);
        return;
    }

    if ((rtcState.writeProtect & 0x80) != 0)
        return;

    switch (address)
    {
        case 0:
            rtcState.clockHalted = (value & 0x80) != 0;
            rtcState.seconds =
                bcdToBinary(static_cast<uint8_t>(value & 0x7F));
            break;
        case 1:
            rtcState.minutes =
                bcdToBinary(static_cast<uint8_t>(value & 0x7F));
            break;

        case 2:
        {
            rtcState.hourMode12 = (value & 0x80) != 0;

            if (rtcState.hourMode12)
            {
                rtcState.hourPM = (value & 0x20) != 0;
                rtcState.hours =
                    bcdToBinary(static_cast<uint8_t>(value & 0x1F));
            }
            else
            {
                rtcState.hourPM = false;
                rtcState.hours =
                    bcdToBinary(static_cast<uint8_t>(value & 0x3F));
            }

            break;
        }

        case 3:
            rtcState.dayOfMonth =
                bcdToBinary(static_cast<uint8_t>(value & 0x3F));
            break;

        case 4:
            rtcState.month =
                bcdToBinary(static_cast<uint8_t>(value & 0x1F));
            break;

        case 5:
            rtcState.dayOfWeek =
                bcdToBinary(static_cast<uint8_t>(value & 0x07));
            break;

        case 6:
        {
            const uint16_t century =
                static_cast<uint16_t>((rtcState.year / 100) * 100);

            rtcState.year =
                static_cast<uint16_t>(
                    century + bcdToBinary(value));
            break;
        }

        case 8:
            rtcState.trickleCharger = value;
            break;

        default:
            break;
    }
}
