// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Drive/D1571.h"

D1571::D1571(int deviceNumber, const std::string& fileName) :
    motorOn(false),
    diskLoaded(false)
{
    setDeviceNumber(deviceNumber);
    d1571Mem.attachPeripheralInstance(this);
    driveCPU.attachIRQLineInstance(&IRQ);
    driveCPU.attachMemoryInstance(&d1571Mem);
    if (!d1571Mem.initialize(fileName))
    {
        throw std::runtime_error("Unable to start drive, ROM not loaded!\n");
    }
    reset();
}

D1571::~D1571() = default;

void D1571::tick()
{
    driveCPU.tick();
    d1571Mem.tick();
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

    d1571Mem.reset();
    driveCPU.reset();
}

bool D1571::canMount(DiskFormat fmt) const
{
    return fmt == DiskFormat::D64 || fmt == DiskFormat::D71;
}

void D1571::updateIRQ()
{
    bool via1IRQ = d1571Mem.getVIA1().checkIRQActive();
    bool via2IRQ = d1571Mem.getVIA2().checkIRQActive();
    bool ciaIRQ = d1571Mem.getCIA().checkIRQActive();
    bool fdcIRQ = d1571Mem.getFDC().checkIRQActive(); // until implemented

    bool any = via1IRQ || via2IRQ || ciaIRQ || fdcIRQ;

    if (any) IRQ.raiseIRQ(IRQLine::D1571_IRQ);
    else IRQ.clearIRQ(IRQLine::D1571_IRQ);
}

bool D1571::mountDisk(const std::string& path)
{
    auto img = DiskFactory::create(path);
    if (!img)
    {
        diskImage.reset();
        diskLoaded = false;
        loadedDiskName.clear();
        lastError = DriveError::NO_DISK;
        return false;
    }

    // Try to load the disk image from file
    if (!img->loadDisk(path)) {
        diskImage.reset();
        diskLoaded = false;
        loadedDiskName.clear();
        lastError = DriveError::NO_DISK;
        return false;
    }

    // Success load it
    diskImage      = std::move(img);
    diskLoaded     = true;
    loadedDiskName = path;
    lastError      = DriveError::NONE;

    currentTrack  = 18;
    currentSector = 0;

    return true;
}

void D1571::unmountDisk()
{
    // Drop the current image
    diskImage.reset();
    diskLoaded = false;
    loadedDiskName.clear();

    // Reset basic geometry/status
    currentTrack  = 0;
    currentSector = 1;
    lastError     = DriveError::NONE;
    status        = DriveStatus::IDLE;
}

bool D1571::readSector(uint8_t track, uint8_t sector, uint8_t* buffer, size_t length)
{
    if (!diskLoaded || !diskImage || buffer == nullptr || length == 0)
    {
        lastError = DriveError::NO_DISK;
        return false;
    }

    std::vector<uint8_t> data = diskImage->readSector(track, sector);
    if (data.empty())
    {
        lastError = DriveError::BAD_SECTOR;
        return false;
    }

    const size_t toCopy = std::min(length, data.size());
    std::memcpy(buffer, data.data(), toCopy);

    currentTrack  = track;
    currentSector = sector;
    lastError     = DriveError::NONE;

    return true;
}
