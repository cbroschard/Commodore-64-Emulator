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
    iecLinesPrimed(false),
    iecListening(false),
    iecRxActive(false),
    iecTalking(false),
    presenceAckDone(false),
    expectingSecAddr(false),
    expectingDataByte(false),
    currentListenSA(0),
    currentTalkSA(0),
    iecRxBitCount(0),
    iecRxByte(0),
    diskLoaded(false),
    diskWriteProtected(false),
    lastError(DriveError::NONE),
    status(DriveStatus::IDLE),
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
    motorOn             = false;

    // Status
    lastError           = DriveError::NONE;
    status              = DriveStatus::IDLE;
    currentTrack        = 17;
    currentSector       = 0;

    // IEC BUS reset
    atnLineLow          = false;
    clkLineLow          = false;
    dataLineLow         = false;
    srqAsserted         = false;

    // IEC Communication
    iecLinesPrimed      = false;
    iecTalking          = false;
    iecListening        = false;
    iecRxActive         = false;
    presenceAckDone     = false;
    expectingSecAddr    = false;
    expectingDataByte   = false;
    currentListenSA     = 0;
    currentTalkSA       = 0;
    iecRxBitCount       = 0;
    iecRxByte           = 0;

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
    if (!iecLinesPrimed)
    {
        forceSyncIEC();
        iecLinesPrimed = true;
    }

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

void D1581::unloadDisk()
{
    diskImage.reset();
    diskLoaded = false;
    loadedDiskName.clear();

    currentDriveError  = DriveError::NO_DISK;
    currentDriveStatus = DriveStatus::IDLE;
}

void D1581::forceSyncIEC()
{
    if (!bus)
        return;

    atnLineLow  = !bus->readAtnLine();
    clkLineLow  = !bus->readClkLine();
    dataLineLow = !bus->readDataLine();

    // Push those levels into the base drive/peripheral logic
    Drive::atnChanged(atnLineLow);
    Drive::driveControlClkLine(clkLineLow);
    Drive::driveControlDataLine(dataLineLow);
}

void D1581::atnChanged(bool atnLow)
{
    if (atnLow == atnLineLow) return; // ignore no change

    atnLineLow = atnLow;
    Drive::atnChanged(atnLineLow);

    // Force clk to release when Atn is asserted by the C64
    if (atnLineLow) peripheralAssertClk(false);

    // Check for falling edge
    d1581mem.getCIA().setFlagLine(!atnLow ? true : false);
}

void D1581::clkChanged(bool clkLow)
{
    if (clkLow == clkLineLow) return; // ignore no change

    clkLineLow = clkLow;
    Drive::driveControlClkLine(clkLineLow);
}

void D1581::dataChanged(bool dataLow)
{
    if (dataLow == dataLineLow) return; // ignore no change

    dataLineLow = dataLow;
    Drive::driveControlDataLine(dataLineLow);
}

void D1581::onListen()
{
    // IEC bus has selected this drive as a listener
    iecListening = true;
    iecTalking   = false;

    listening = true;
    talking   = false;
}

void D1581::onUnListen()
{
    peripheralAssertData(false);
    peripheralAssertClk(false);
    peripheralAssertSrq(false);
}

void D1581::onTalk()
{
    iecTalking   = true;
    iecListening = false;

    talking   = true;
    listening = false;
    iecRxActive = false;
    iecRxBitCount = 0;
    iecRxByte = 0;

    // After TALK, the next byte from the C64 is a secondary address
    expectingSecAddr  = true;
    expectingDataByte = false;
    currentSecondaryAddress = 0xFF;

    peripheralAssertClk(false);

    #ifdef Debug
    std::cout << "[D1581] onTalk() device=" << int(deviceNumber)
              << " talking=1 listening=0\n";
    #endif
}

void D1581::onUnTalk()
{
    iecTalking = false;
    talking    = false;

    expectingSecAddr  = false;
    expectingDataByte = false;

    peripheralAssertData(false);
    peripheralAssertClk(false);
    peripheralAssertSrq(false);
}

void D1581::onSecondaryAddress(uint8_t sa)
{
    currentSecondaryAddress = sa;

    // We’ve now consumed the secondary address; next bytes are data/commands
    expectingSecAddr  = false;
    expectingDataByte = true;

    #ifdef Debug
    const char* meaning = "";
    if (sa == 0)
        meaning = " (LOAD channel)";
    else if (sa == 1)
        meaning = " (SAVE channel)";
    else if (sa == 15)
        meaning = " (COMMAND channel)";

    std::cout << "[D1581] onSecondaryAddress() device=" << int(deviceNumber)
              << " sa=" << int(sa) << meaning << "\n";
    #endif
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
