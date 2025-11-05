// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "D1571.h"

D1571::D1571(int deviceNumber) :
    motorOn(false),
    diskLoaded(false)
{

}

D1571::~D1571() = default;

void D1571::tick()
{
    // TODO: Implement
}

void D1571::reset()
{
    motorOn = false;
    loadedDiskName.clear();
    diskLoaded = false;
    lastError = DriveError::NONE;
    status = DriveStatus::IDLE;
    currentTrack = 0;
    currentSector = 1;
}

bool D1571::canMount(DiskFormat fmt) const
{
    return fmt == DiskFormat::D64 || fmt == DiskFormat::D71;
}
