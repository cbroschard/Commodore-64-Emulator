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
    currentSide(0),
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

    d1581mem.reset();
    driveCPU.reset();
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

void D1581::syncTrackFromFDC()
{
    auto* fdc = getFDC();
    if (!fdc) return;

    currentTrack = fdc->getCurrentTrack();
}

void D1581::updateIRQ()
{
    bool ciaIRQ = d1581mem.getCIA().checkIRQActive();
    bool fdcIRQ = d1581mem.getFDC().checkIRQActive();

    bool any = ciaIRQ || fdcIRQ;

    if (any) irq.raiseIRQ(IRQLine::D1581_IRQ);
    else irq.clearIRQ(IRQLine::D1581_IRQ);
}

bool D1581::fdcReadSector(uint8_t track, uint8_t sector, uint8_t* buffer, size_t length)
{
    if (!diskLoaded || !diskImage || !buffer || length == 0)
    {
        lastError = DriveError::NO_DISK;
        return false;
    }

    const uint16_t d81Track = mapFdcTrackToD81Track(track);
    auto data = diskImage->readSector((uint8_t)d81Track, sector);
    if (data.empty())
    {
        lastError = DriveError::BAD_SECTOR;
        return false;
    }

    // Prefer: enforce full sector size (512 for 1581)
    if (data.size() < length) data.resize(length, 0x00);
    std::memcpy(buffer, data.data(), length);

    currentTrack  = track;
    currentSector = sector;
    lastError     = DriveError::NONE;
    return true;
}

bool D1581::fdcWriteSector(uint8_t track, uint8_t sector, const uint8_t* buffer, size_t length)
{
    if (!diskLoaded || !diskImage || !buffer || length == 0)
    {
        lastError = DriveError::NO_DISK;
        return false;
    }

    if (fdcIsWriteProtected()) return false;

    const uint16_t d81Track = mapFdcTrackToD81Track(track);
    std::vector<uint8_t> data(buffer, buffer + length);
    return diskImage->writeSector((uint8_t)d81Track, sector, data);
}

void D1581::loadDisk(const std::string& path)
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

    // Hard reset in case user switched disk
    reset();

    // Success load it
    diskImage      = std::move(img);
    diskLoaded     = true;
    loadedDiskName = path;
    lastError      = DriveError::NONE;
}

uint16_t D1581::mapFdcTrackToD81Track(uint8_t fdcTrack) const
{
    const uint16_t cyl  = uint16_t(fdcTrack);        // 0..79
    const uint16_t side = uint16_t(getCurrentSide() & 1); // 0/1
    return (side * 80) + (cyl + 1);                  // 1..160
}
