// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Drive/D1581.h"
#include "IECBUS.h"

D1581::D1581(int deviceNumber, const std::string& romName) :
    motorOn(false),
    atnLineLow(false),
    clkLineLow(false),
    dataLineLow(false),
    srqAsserted(false),
    diskLoaded(false),
    diskWriteProtected(false)
{
    setDeviceNumber(deviceNumber);
    reset();
}

D1581::~D1581() = default;

void D1581::reset()
{
    motorOn = false;
    loadedDiskName.clear();
    diskLoaded = false;
    diskWriteProtected = false;
    lastError = DriveError::NONE;
    status = DriveStatus::IDLE;
    currentTrack = 18;
    currentSector = 0;

    // IEC BUS reset
    atnLineLow         = false;
    clkLineLow         = false;
    dataLineLow        = false;
    srqAsserted        = false;
}

void D1581::tick()
{
    // Always call Drive tick first
    Drive::tick();
    driveCPU.tick();
    d1581Mem.tick();
}

bool D1581::canMount(DiskFormat fmt) const
{
    return fmt == DiskFormat::D81;
}
