// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
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
    if (!rdr.readU8(rtcState.seconds))
        return false;

    if (!rdr.readU8(rtcState.minutes))
        return false;

    if (!rdr.readU8(rtcState.hours))
        return false;

    if (!rdr.readU8(rtcState.dayOfWeek))
        return false;

    if (!rdr.readU8(rtcState.dayOfMonth))
        return false;

    if (!rdr.readU8(rtcState.month))
        return false;

    if (!rdr.readU16(rtcState.year))
        return false;

    // Current serial transaction state
    if (!rdr.readBool(wireState.chipEnabled))
        return false;

    uint8_t phase = 0;

    if (!rdr.readU8(phase))
        return false;

    if (phase > static_cast<uint8_t>(TransferPhase::Ignore))
        return false;

    wireState.phase =
        static_cast<TransferPhase>(phase);

    if (!rdr.readU8(wireState.shiftRegister))
        return false;

    if (!rdr.readU8(wireState.outputShiftRegister))
        return false;

    if (!rdr.readU8(wireState.bitCount))
        return false;

    if (!rdr.readU8(wireState.transferIndex))
        return false;

    if (!rdr.readU8(wireState.command))
        return false;

    if (!rdr.readU8(wireState.address))
        return false;

    if (!rdr.readBool(wireState.ramSelected))
        return false;

    if (!rdr.readBool(wireState.readOperation))
        return false;

    if (!rdr.readBool(wireState.burstOperation))
        return false;

    if (!rdr.readBool(wireState.dataOut))
        return false;

    // Battery-backed CMOS RAM
    for (uint8_t& value : cmosRAM)
    {
        if (!rdr.readU8(value))
            return false;
    }

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

        if (wireState.ramSelected &&
            wireState.burstOperation)
        {
            ++wireState.transferIndex;

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
                if (wireState.burstOperation)
                {
                    if (wireState.transferIndex < CMOS_RAM_SIZE)
                    {
                        cmosRAM[wireState.transferIndex] =
                            wireState.shiftRegister;

                        ++wireState.transferIndex;
                    }

                    if (wireState.transferIndex >= CMOS_RAM_SIZE)
                        wireState.phase = TransferPhase::Ignore;
                }
                else
                {
                    if (wireState.address < CMOS_RAM_SIZE)
                    {
                        cmosRAM[wireState.address] =
                            wireState.shiftRegister;
                    }

                    wireState.phase = TransferPhase::Ignore;
                }
            }
            else
            {
                // Clock-register writes come later.
                wireState.phase = TransferPhase::Ignore;
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
            // Clock-register and clock-burst reads come later.
            wireState.outputShiftRegister = 0xFF;
        }
    }
    else
    {
        wireState.phase = TransferPhase::WriteData;
    }
}
