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

}

IDE64RTC::~IDE64RTC() = default;

void IDE64RTC::reset()
{
    wireState.latch     = 0;
    wireState.clkSeen   = false;
    wireState.dataIn    = false;
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

    wrtr.writeU8(wireState.latch);
    wrtr.writeBool(wireState.clkSeen);
    wrtr.writeBool(wireState.dataIn);
}

bool IDE64RTC::loadState(StateReader& rdr)
{
    if (!rdr.readU8(rtcState.seconds))      return false;
    if (!rdr.readU8(rtcState.minutes))      return false;
    if (!rdr.readU8(rtcState.hours))        return false;
    if (!rdr.readU8(rtcState.dayOfWeek))    return false;
    if (!rdr.readU8(rtcState.dayOfMonth))   return false;
    if (!rdr.readU8(rtcState.month))        return false;
    if (!rdr.readU16(rtcState.year))         return false;

    if (!rdr.readU8(wireState.latch))       return false;
    if (!rdr.readBool(wireState.clkSeen))   return false;
    if (!rdr.readBool(wireState.dataIn))    return false;

    return true;
}

uint8_t IDE64RTC::readByte() const
{
    return 0xFF;
}

void IDE64RTC::writeByte(uint8_t value)
{

}
