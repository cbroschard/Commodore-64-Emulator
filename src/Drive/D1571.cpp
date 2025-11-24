// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Drive/D1571.h"
#include "IECBUS.h"

D1571::D1571(int deviceNumber, const std::string& romName) :
    motorOn(false),
    atnLineLow(false),
    clkLineLow(false),
    dataLineLow(false),
    srqAsserted(false),
    iecListening(false),
    iecTalking(false),
    presenceAckDone(false),
    expectingSecAddr(false),
    expectingDataByte(false),
    currentListenSA(0),
    currentTalkSA(0),
    currentSide(1),
    fastSerialOutput(false),
    twoMHzMode(false),
    atnAckEnabled(true),
    atnAckPullsDataLow(false),
    ackInProgress(false),
    atnAckCompletedThisAtn(false),
    iecRxActive(false),
    iecRxBitCount(0),
    iecRxByte(0),
    diskLoaded(false),
    diskWriteProtected(false),
    currentTrack(0),
    currentSector(0)
{
    setDeviceNumber(deviceNumber);
    d1571Mem.attachPeripheralInstance(this);
    driveCPU.attachIRQLineInstance(&IRQ);
    driveCPU.attachMemoryInstance(&d1571Mem);

    if (!d1571Mem.initialize(romName))
    {
        throw std::runtime_error("Unable to start drive, ROM not loaded!\n");
    }

    reset();
}

D1571::~D1571() = default;

void D1571::tick()
{
    Drive::tick();
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
    atnLineLow                  = false;
    clkLineLow                  = false;
    dataLineLow                 = false;
    srqAsserted                 = false;
    iecListening                = false;
    iecTalking                  = false;
    presenceAckDone             = false;
    expectingSecAddr            = false;
    expectingDataByte           = false;
    atnAckEnabled               = true;
    atnAckPullsDataLow          = false;
    ackInProgress               = false;
    atnAckCompletedThisAtn      = false;
    currentListenSA             = 0;
    currentTalkSA               = 0;
    iecRxActive                 = false;
    iecRxBitCount               = 0;
    iecRxByte                   = 0;

    // 1571 Runtime Properties reset
    currentSide         = 0;
    fastSerialOutput    = false;
    twoMHzMode          = false;
    currentTrack        = 0;
    currentSector       = 0;

    // Force reset state
    applyDataLine();

    d1571Mem.reset();
    driveCPU.reset();
}

void D1571::setSRQAsserted(bool state)
{
    srqAsserted = state;
}

void D1571::setFastSerialBusDirection(bool output)
{
    fastSerialOutput = output;
}

void D1571::setBurstClock2MHz(bool enable)
{
    twoMHzMode = enable;
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

bool D1571::fdcWriteSector(uint8_t track, uint8_t sector, const uint8_t* buffer, size_t length)
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

void D1571::applyDataLine()
{
    bool drivePullsDataLow = atnAckPullsDataLow;
    dataLineLow = drivePullsDataLow;

    peripheralAssertData(drivePullsDataLow);
}

void D1571::beginAtnAck()
{
    if (ackInProgress)
        return;

    ackInProgress = true;

    // ATN is low here: device acknowledges by pulling DATA low.
    // DO NOT touch CLK here.
    std::cout << "[D1571] Begin ATN ACK (DATA low)\n";
    driveControlDataLine(true);   // pull DATA low
}

void D1571::endAtnAck()
{
    if (!ackInProgress)
        return;

    ackInProgress = false;

    std::cout << "[D1571] End ATN ACK (release DATA)\n";
    driveControlDataLine(false);  // release DATA
}

void D1571::updateAtnAckState()
{
    if (atnLineLow && atnAckEnabled)
    {
        // ATN asserted and ATNA enabled -> ensure ACK is active
        beginAtnAck();
    }
    else
    {
        // Either ATN released or ATNA disabled -> no ACK
        endAtnAck();
        atnAckCompletedThisAtn = false;
    }
}

void D1571::atnChanged(bool atnLow)
{
    bool prev = atnLineLow;
    atnLineLow = atnLow;

    std::cout << "[D1571] atnChanged: atnLow=" << atnLineLow
              << " (prev=" << prev << ")\n";

    // Keep VIA in sync with the new ATN level
    auto& via1 = d1571Mem.getVIA1();
    via1.setIECInputLines(atnLineLow, clkLineLow, dataLineLow);

    // Optionally drive ATN-ACK here if this is the selected device
    if (atnLineLow)
    {
        // ATN just went LOW → start ATN ACK handshake
        beginAtnAck();
    }
    else
    {
        // ATN went HIGH → make sure ACK is cleared
        if (ackInProgress)
            endAtnAck();
    }
}

void D1571::setAtnAckEnabled(bool enabled)
{
    atnAckEnabled = enabled;
    updateAtnAckState();
}

void D1571::clkChanged(bool clkLow)
{
    bool prevClkLow  = clkLineLow;
    clkLineLow       = clkLow;

    bool prevClkHigh = !prevClkLow;
    bool clkHigh     = !clkLow;

    // Edge detection on the bus CLK line
    bool rising  = (!prevClkHigh && clkHigh);    // low -> high
    bool falling = ( prevClkHigh && !clkHigh );  // high -> low
    bool edge    = rising || falling;

    if (iecListening && !atnLineLow && rising)
    {
        std::cout << "[D1571] (LISTEN PHASE) CLK rising, drive should sample DATA here\n";
    }

    std::cout << "[D1571] clkChanged atnLow=" << atnLineLow
              << " clkLow=" << clkLow
              << " dataLow=" << dataLineLow
              << " ackInProgress=" << ackInProgress
              << " rising=" << rising
              << " falling=" << falling << "\n";

    // ---- ATN ACK hardware handshake ----
    if (atnLineLow && ackInProgress && edge)
    {
        std::cout << "[D1571] Complete ATN ACK on CLK "
                  << (falling ? "falling" : "rising") << " edge\n";

        endAtnAck();

        // Keep VIA's view of the IEC lines in sync after DATA/CLK changes
        auto& via1 = d1571Mem.getVIA1();
        via1.setIECInputLines(atnLineLow, clkLineLow, dataLineLow);
        return;
    }

    // ---------- ATN HIGH: LISTENER PHASE ----------
    if (!atnLineLow && iecListening)
    {
        // 1) Presence ACK (what you already had)
        if (rising && !presenceAckDone)
        {
            // We’re a listener, ATN is high, C64 is pulsing CLK:
            // announce ourselves by pulling DATA low once.

            std::cout << "[D1571] LISTENER PRESENCE ACK: pulling DATA low\n";

            // Use your existing helper that logs:
            // [Drive] driveControlDataLine: dataLow=1 (dev=8)
            driveControlDataLine(true);    // dataLow = true => pull line low

            presenceAckDone = true;
        }
        if (presenceAckDone && falling)
        {
            std::cout << "[D1571] LISTENER PRESENCE ACK: releasing DATA\n";
            driveControlDataLine(false);
            presenceAckDone = false;
        }

        // 2) DATA RECEPTION (C64 -> drive, after presence ACK)
        //
        // IEC spec: receiver samples on CLOCK falling edge when ATN is high.
        // We also skip while presenceAckDone is true so we don't count the
        // presence-ACK edge as data.
        if (falling && !presenceAckDone && !ackInProgress)
        {
            // On the bus, a '1' is DATA released (high), which in your
            // logic is dataLow == false.
            bool bit = !dataLineLow;   // DATA high -> 1, DATA low -> 0

            // LSB-first assemble: shift right, drop new bit into bit 7.
            iecRxByte = (iecRxByte >> 1) | (bit ? 0x80 : 0x00);
            iecRxBitCount++;

            if (iecRxBitCount == 8)
            {
                std::cout << "[D1571] LISTEN RX byte: $"
                          << std::hex << int(iecRxByte) << std::dec
                          << " (from C64 while listening)\n";

                // TODO: later: feed this into your DOS command parser.
                // For now, just reset for the next byte.
                iecRxBitCount = 0;
                iecRxByte     = 0;
            }
        }
    }

    // Normal path: just update VIA with the new CLK level
    auto& via1 = d1571Mem.getVIA1();
    via1.setIECInputLines(atnLineLow, clkLineLow, dataLineLow);
    via1.onClkEdge(rising, falling);
}

void D1571::dataChanged(bool dataLow)
{
    if (iecListening && !atnLineLow)
    {
        std::cout << "[D1571] (LISTEN PHASE) seeing C64 data bit; clkLow="
                  << clkLineLow << " dataLow=" << dataLineLow << "\n";
    }

    // Bus DATA line changed (dataLow=true -> line pulled low, false -> high).
    dataLineLow = dataLow;

    auto& via1 = d1571Mem.getVIA1();
    via1.setIECInputLines(atnLineLow, clkLineLow, dataLineLow);

    std::cout << "[D1571] dataChanged atnLow=" << atnLineLow << " clkLow=" << clkLineLow << " dataLow=" << dataLineLow << " ackInProgress=" << ackInProgress << "\n";
}

void D1571::onListen()
{
    // IEC bus has selected this drive as a listener
    iecListening = true;
    iecTalking   = false;

    listening = true;
    talking   = false;
    iecRxActive   = true;
    iecRxBitCount = 0;
    iecRxByte     = 0;

    // We're about to receive a secondary address byte after LISTEN
    presenceAckDone   = false;   // so we do the LISTEN presence ACK
    expectingSecAddr  = true;    // first byte after LISTEN is secondary address
    expectingDataByte = false;
    currentSecondaryAddress = 0xFF;  // "none" / invalid

    std::cout << "[D1571] onListen() device=" << int(deviceNumber)
              << " listening=1 talking=0\n";
}

void D1571::onUnListen()
{
    iecListening = false;
    listening    = false;
    iecRxActive = false;
    iecRxBitCount = 0;
    iecRxByte = 0;

    expectingSecAddr  = false;
    expectingDataByte = false;

    std::cout << "[D1571] onUnListen() device=" << int(deviceNumber) << "\n";
}

void D1571::onTalk()
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

    std::cout << "[D1571] onTalk() device=" << int(deviceNumber)
              << " talking=1 listening=0\n";
}

void D1571::onUnTalk()
{
    iecTalking = false;
    talking    = false;
    iecRxActive = false;
    iecRxBitCount = 0;
    iecRxByte = 0;


    expectingSecAddr  = false;
    expectingDataByte = false;

    std::cout << "[D1571] onUnTalk() device=" << int(deviceNumber) << "\n";
}

void D1571::onSecondaryAddress(uint8_t sa)
{
    currentSecondaryAddress = sa;

    // We’ve now consumed the secondary address; next bytes are data/commands
    expectingSecAddr  = false;
    expectingDataByte = true;

    const char* meaning = "";
    if (sa == 0)
        meaning = " (LOAD channel)";
    else if (sa == 1)
        meaning = " (SAVE channel)";
    else if (sa == 15)
        meaning = " (COMMAND channel)";

    std::cout << "[D1571] onSecondaryAddress() device=" << int(deviceNumber)
              << " sa=" << int(sa) << meaning << "\n";
}
