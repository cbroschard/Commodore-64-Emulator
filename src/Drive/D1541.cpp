// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Drive/D1541.h"

D1541::D1541(int deviceNumber) :
    // Power on defaults
    motorOn(false),
    diskLoaded(false),
    SRQAsserted(false),
    currentTrack(17),
    currentSector(0)
{
    setDeviceNumber(deviceNumber);

    driveCPU.attachMemoryInstance(&d1541mem);
    driveCPU.attachIRQLineInstance(d1541mem.getIRQLine());
    d1541mem.getVIA1().attachPeripheralInstance(this, D1541VIA::VIARole::VIA1_DataHandler);
    d1541mem.getVIA2().attachPeripheralInstance(this, D1541VIA::VIARole::VIA2_AtnMonitor);
}

D1541::~D1541() = default;


void D1541::reset()
{
    // Mechanics
    motorOn = false;

    // Status
    lastError           = DriveError::NONE;
    status              = DriveStatus::IDLE;

    // Disk
    diskLoaded          = false;
    diskWriteProtected  = false;
    currentTrack        = 17;
    currentSector       = 0;
    halfTrackPos        = currentTrack * 2;
    loadedDiskName.clear();

    // IEC
    SRQAsserted = false;

    // CHIPS
    d1541mem.reset();
    driveCPU.reset();
}

void D1541::tick(uint32_t cycles)
{
    while(cycles > 0)
    {
        driveCPU.tick();
        uint32_t dc = driveCPU.getElapsedCycles();
        if(dc == 0) dc = 1;

        Drive::tick(dc);
        d1541mem.tick();
        cycles -= dc;
    }
}

bool D1541::initialize(const std::string& loRom, const std::string& hiRom)
{
    if (!d1541mem.initialize(loRom, hiRom))
    {
        return false;
    }

    reset();
    return true;
}

void D1541::loadDisk(const std::string& path)
{
    diskWriteProtected = false;
    auto img = DiskFactory::create(path);
    if (!img)
    {
        diskImage.reset();
        diskLoaded = false;
        loadedDiskName.clear();
        lastError = DriveError::NO_DISK;
        return;
    }

    // Try to load the disk image from file
    if (!img->loadDisk(path))
    {
        diskImage.reset();
        diskLoaded = false;
        loadedDiskName.clear();
        lastError = DriveError::NO_DISK;
        return;
    }

    // Success load it
    diskImage      = std::move(img);
    diskLoaded     = true;
    loadedDiskName = path;
    lastError      = DriveError::NONE;

    currentTrack  = 17;
    currentSector = 0;
    halfTrackPos = currentTrack * 2;
}

void D1541::unloadDisk()
{
    diskImage.reset();  // Reset disk image by assigning a fresh instance
    loadedDiskName.clear();

    diskLoaded      = false;
    currentTrack    = 0;
    currentSector   = 1;
    lastError       = DriveError::NONE;
    status          = DriveStatus::IDLE;
}

void D1541::clkChanged(bool clkState)
{
    if (bus)
    {
        bus->setClkLine(!clkState);
    }
}

void D1541::dataChanged(bool dataState)
{
    if (bus)
    {
        bus->setDataLine(!dataState);
    }
}
