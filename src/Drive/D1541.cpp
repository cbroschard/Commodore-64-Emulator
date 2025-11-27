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
    lastError(DriveError::NONE),
    status(DriveStatus::IDLE),
    currentTrack(0),
    currentSector(1)
{
   setDeviceNumber(deviceNumber);
}

D1541::~D1541() = default;

bool D1541::canMount(DiskFormat fmt) const
{
    return fmt == DiskFormat::D64;
}

void D1541::tick()
{
    Drive::tick();
    d1541mem.tick();
    driveCPU.tick();
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

void D1541::reset()
{
    motorOn = false;
    loadedDiskName.clear();
    diskLoaded = false;
    SRQAsserted = false;
    lastError = DriveError::NONE;
    status = DriveStatus::IDLE;
    currentTrack = 0;
    currentSector = 1;
    d1541mem.reset();
    driveCPU.attachMemoryInstance(&d1541mem);
    driveCPU.attachIRQLineInstance(d1541mem.getIRQLine());
    d1541mem.getVIA1().attachPeripheralInstance(this, D1541VIA::VIARole::VIA1_DataHandler);
    d1541mem.getVIA2().attachPeripheralInstance(this, D1541VIA::VIARole::VIA2_AtnMonitor);
    driveCPU.reset();
}

void D1541::loadDisk(const std::string& path)
{
    diskImage = DiskFactory::create(path);
    if (diskImage->loadDisk(path))
    {
        // Extract and store only the filename part
        loadedDiskName = path.substr(path.find_last_of("/\\") + 1);
        diskLoaded = true;
        status = DriveStatus::READY;
        currentTrack = 18;  // Directory track by default
        currentSector = 0;  // First sector of directory
    }
    else
    {
        diskLoaded = false;
        loadedDiskName.clear();
        status = DriveStatus::IDLE;
        lastError = DriveError::NO_DISK;
        throw std::runtime_error("Failed to load disk: " + path);
    }
}

void D1541::unloadDisk()
{
    diskImage.reset();  // Reset disk image by assigning a fresh instance
    diskLoaded = false;
    loadedDiskName.clear();
    currentTrack = 0;
    currentSector = 1;
    status = DriveStatus::IDLE;
}

bool D1541::isDiskLoaded() const
{
    return diskLoaded;
}

uint8_t D1541::getCurrentTrack() const
{
    return currentTrack;
}

uint8_t D1541::getCurrentSector() const
{
    return currentSector;
}

void D1541::startMotor()
{
    if (!motorOn)
    {
        motorOn = true;
    }
}

void D1541::stopMotor()
{
    motorOn = false;
}

bool D1541::isMotorOn() const
{
    return motorOn;
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

bool D1541::isSRQAsserted() const
{

    return SRQAsserted;
}

void D1541::setSRQAsserted(bool state)
{
    SRQAsserted = state;
    if (bus)
    {
        bus->setSrqLine(state);
    }
}
