// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Drive/D1571.h"
#include "IECBUS.h"

D1571::D1571(int deviceNumber, const std::string& fileName) :
    motorOn(false),
    bus(nullptr),
    atnLineLow(false),
    clkLineLow(false),
    dataLineLow(false),
    srqAsserted(false),
    currentSide(1),
    fastSerialOutput(false),
    twoMHzMode(false),
    atnAckEnabled(false),
    atnAckPullsDataLow(false),
    dataOutPullsLow(false),
    diskLoaded(false),
    diskWriteProtected(false)
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
    diskWriteProtected = false;
    lastError = DriveError::NONE;
    status = DriveStatus::IDLE;
    currentTrack = 18;
    currentSector = 0;
    densityCode = 2;

    // IEC BUS reset
    atnLineLow         = false;
    clkLineLow         = false;
    dataLineLow        = false;
    srqAsserted        = false;
    atnAckEnabled      = false;
    atnAckPullsDataLow = false;
    dataOutPullsLow    = false;

    // 1571 Runtime Properties reset
    currentSide = 0;
    fastSerialOutput = false;
    twoMHzMode = false;

    // Force reset state
    applyDataLine();

    d1571Mem.reset();
    driveCPU.reset();
}

void D1571::setSRQAsserted(bool state)
{
    srqAsserted = state;
    if (bus)
    {
        bus->setSrqLine(state);
    }
}

void D1571::setFastSerialBusDirection(bool output)
{
    fastSerialOutput = output; // bool member, hook up later
}

void D1571::setBurstClock2MHz(bool enable)
{
    twoMHzMode = enable;       // bool member, hook up later to timing
}

bool D1571::getByteReadyLow() const
{
    auto* fdc = getFDC();
    if (!fdc) return false;

    bool drqActive = fdc->checkDRQActive();
    bool intrqActive = fdc->checkIRQActive();
    return drqActive || intrqActive;

}

void D1571::syncTrackFromFDC()
{
    auto* fdc = getFDC();
    if (!fdc) return;

    currentTrack = fdc->getCurrentTrack();
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
    bool fdcIRQ = d1571Mem.getFDC().checkIRQActive();

    bool any = via1IRQ || via2IRQ || ciaIRQ || fdcIRQ;

    if (any) IRQ.raiseIRQ(IRQLine::D1571_IRQ);
    else IRQ.clearIRQ(IRQLine::D1571_IRQ);
}

void D1571::loadDisk(const std::string& path)
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

    currentTrack  = 18;
    currentSector = 0;
}

void D1571::unloadDisk()
{
    // Drop the current image
    diskWriteProtected = false;
    diskImage.reset();
    diskLoaded = false;
    loadedDiskName.clear();

    // Reset basic geometry/status
    currentTrack  = 0;
    currentSector = 1;
    lastError     = DriveError::NONE;
    status        = DriveStatus::IDLE;
}

bool D1571::fdcReadSector(uint8_t track, uint8_t sector, uint8_t* buffer, size_t length)
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

bool D1571::fdcWriteSector(uint8_t track,
                           uint8_t sector,
                           const uint8_t* buffer,
                           size_t length)
{
    // Basic sanity
    if (!diskLoaded || !diskImage || buffer == nullptr || length == 0)
    {
        lastError = DriveError::NO_DISK;   // or WRITE_FAILED etc.
        return false;
    }

    constexpr size_t SECTOR_SIZE = 256;  // matches Disk::SECTOR_SIZE

    const size_t toCopy = std::min(length, SECTOR_SIZE);

    std::vector<uint8_t> data;
    data.assign(buffer, buffer + toCopy);

    if (data.size() < SECTOR_SIZE)
        data.resize(SECTOR_SIZE, 0x00);

    bool ok = diskImage->writeSector(track, sector, data);

    if (!ok)
    {
        lastError = DriveError::BAD_SECTOR;  // or WRITE_FAILED
        return false;
    }

    lastError     = DriveError::NONE;
    currentTrack  = track;
    currentSector = sector;
    return true;
}

bool D1571::fdcIsWriteProtected() const
{
    return diskWriteProtected;
}

std::vector<unsigned char> D1571::getDirectoryListing()
{
    // TODO: implement via diskImage
    return {};
}

std::vector<unsigned char> D1571::loadFileByName(const std::string& name)
{
    // TODO: implement via diskImage
    return {};
}

void D1571::applyDataLine()
{
    bool drivePullsDataLow = dataOutPullsLow || atnAckPullsDataLow;
    dataLineLow = drivePullsDataLow;

    if (bus) bus->setDataLine(drivePullsDataLow);
}

void D1571::updateAtnAckState()
{
    bool newAckPullsLow = atnAckEnabled && atnLineLow;

    if (newAckPullsLow != atnAckPullsDataLow)
    {
        atnAckPullsDataLow = newAckPullsLow;
        applyDataLine(); // push updated combined state to the bus
    }
}

void D1571::atnChanged(bool atnLow)
{
    atnLineLow = atnLow;
    updateAtnAckState();
}

void D1571::setAtnAckEnabled(bool enabled)
{
    atnAckEnabled = enabled;
    updateAtnAckState();
}

void D1571::clkChanged(bool clkState)
{
    clkLineLow = clkState;
    if (bus) bus->setClkLine(clkState);
}

void D1571::dataChanged(bool dataState)
{
    dataOutPullsLow = dataState;
    applyDataLine();
}
