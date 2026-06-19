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
    wireState.chipEnabled = false;
    wireState.shiftRegister = 0;
    wireState.bitCount = 0;
    wireState.dataOut = true;
}

void IDE64RTC::setChipEnabled(bool enabled)
{
    if (enabled && !wireState.chipEnabled)
    {
        // Beginning of a new DS1302 transaction.
        wireState.shiftRegister = 0;
        wireState.bitCount = 0;
        wireState.dataOut = true;
    }
    else if (!enabled && wireState.chipEnabled)
    {
        // End of the transaction.
        wireState.shiftRegister = 0;
        wireState.bitCount = 0;
        wireState.dataOut = true;
    }

    wireState.chipEnabled = enabled;
}

void IDE64RTC::saveState(StateWriter& wrtr) const
{
    wrtr.writeU8(rtcState.seconds);
    wrtr.writeU8(rtcState.minutes);
    wrtr.writeU8(rtcState.hours);
    wrtr.writeU8(rtcState.dayOfWeek);
    wrtr.writeU8(rtcState.dayOfMonth);
    wrtr.writeU8(rtcState.month);
    wrtr.writeU16(rtcState.year);

    wrtr.writeBool(wireState.chipEnabled);
    wrtr.writeU8(wireState.shiftRegister);
    wrtr.writeU8(wireState.bitCount);
    wrtr.writeBool(wireState.dataOut);

    for (uint8_t value : cmosRAM)
        wrtr.writeU8(value);
}

bool IDE64RTC::loadState(StateReader& rdr)
{
    if (!rdr.readU8(rtcState.seconds))          return false;
    if (!rdr.readU8(rtcState.minutes))          return false;
    if (!rdr.readU8(rtcState.hours))            return false;
    if (!rdr.readU8(rtcState.dayOfWeek))        return false;
    if (!rdr.readU8(rtcState.dayOfMonth))       return false;
    if (!rdr.readU8(rtcState.month))            return false;
    if (!rdr.readU16(rtcState.year))            return false;

    if (!rdr.readBool(wireState.chipEnabled))   return false;
    if (!rdr.readU8(wireState.shiftRegister))   return false;
    if (!rdr.readU8(wireState.bitCount))        return false;
    if (!rdr.readBool(wireState.dataOut))       return false;

    for (uint8_t& value : cmosRAM)
    {
        if (!rdr.readU8(value))
            return false;
    }

    return true;
}

uint8_t IDE64RTC::readByte()
{
    return 0xFF;
}

void IDE64RTC::writeByte(uint8_t value)
{
    if (!wireState.chipEnabled)
        return;

    if (wireState.bitCount >= 8)
        return;

    const uint8_t bit = value & 0x01;

    wireState.shiftRegister |=
        static_cast<uint8_t>(bit << wireState.bitCount);

    ++wireState.bitCount;
}
