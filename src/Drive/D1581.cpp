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
    diskWriteProtected(false),
    currentTrack(17),
    currentSector(0)
{
    setDeviceNumber(deviceNumber);
    d1581mem.attachPeripheralInstance(this);
    driveCPU.attachMemoryInstance(&d1581mem);
    driveCPU.attachIRQLineInstance(&irq);

    if (!d1581mem.initialize(romName))
    {
        throw std::runtime_error("Unable to start drive, ROM not loaded!\n");
    }

    reset();
}

D1581::~D1581() = default;

void D1581::reset()
{
    motorOn = false;

    // Status
    loadedDiskName.clear();
    diskLoaded          = false;
    diskWriteProtected  = false;
    lastError           = DriveError::NONE;
    status              = DriveStatus::IDLE;
    currentTrack        = 17;
    currentSector       = 0;

    // IEC BUS reset
    atnLineLow         = false;
    clkLineLow         = false;
    dataLineLow        = false;
    srqAsserted        = false;

    // Reset actual line states
    peripheralAssertClk(false);  // Release Clock
    peripheralAssertData(false); // Release Data
    peripheralAssertSrq(false);  // Release SRQ

    if (bus)
    {
        bus->unTalk(deviceNumber);
        bus->unListen(deviceNumber);
    }
}

void D1581::tick(uint32_t cycles)
{
    while(cycles > 0)
    {
        driveCPU.tick();
        uint32_t dc = driveCPU.getElapsedCycles();
        if(dc == 0) dc = 1;

        Drive::tick(dc);
        d1581mem.tick(dc);
        cycles -= dc;
    }
}

bool D1581::canMount(DiskFormat fmt) const
{
    return fmt == DiskFormat::D81;
}
