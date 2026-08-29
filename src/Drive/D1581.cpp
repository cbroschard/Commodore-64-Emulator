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
    diskLoaded(false),
    diskWriteProtected(false),
    lastError(DriveError::NONE),
    currentTrack(17),
    currentSector(0),
    uiTrack(17),
    uiSector(0),
    uiLedWasOn(false),
    powerLedOn(false),
    activityLedOn(false),
    activityPulseFrames(0)
{
    setDeviceNumber(deviceNumber);
    d1581mem.attachPeripheralInstance(this);
    d1581mem.attachCPUInstance(&driveCPU);
    driveCPU.attachCPUBusInstance(&d1581mem);
    driveCPU.attachIRQLineInstance(&irq);

    if (!d1581mem.initialize(romName))
    {
        throw std::runtime_error("Unable to start drive, ROM not loaded!\n");
    }

    reset();
}

D1581::~D1581()
{
    flushAndSaveDisk();
}

void D1581::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("D158");

    // Version
    wrtr.writeU32(2);

    // Identity / basic config
    wrtr.writeU8(static_cast<uint8_t>(deviceNumber));

    // Disk attachment info
    wrtr.writeBool(diskLoaded);
    wrtr.writeBool(diskWriteProtected);
    wrtr.writeString(loadedDiskName);

    // Drive configuration / status
    wrtr.writeU8(currentSide);
    wrtr.writeU8(static_cast<uint8_t>(lastError));
    wrtr.writeU8(static_cast<uint8_t>(currentDriveStatus));

    // CPU state
    driveCPU.saveStatePayload(wrtr);
    driveCPU.saveStateExtendedPayload(wrtr);

    // Mechanics / runtime state
    wrtr.writeBool(motorOn);
    wrtr.writeU8(currentTrack);
    wrtr.writeU8(currentSector);

    // IEC protocol + line levels
    wrtr.writeBool(atnLineLow);
    wrtr.writeBool(clkLineLow);
    wrtr.writeBool(dataLineLow);
    wrtr.writeBool(srqAsserted);

    wrtr.writeBool(iecLinesPrimed);

    // Memory + chips in memory map
    d1581mem.saveState(wrtr);

    d1581mem.getCIA().saveState(wrtr);
    d1581mem.getFDC().saveState(wrtr);

    wrtr.endChunk();
}

bool D1581::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "D158", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    // Version
    uint32_t ver = 0;
    if (!rdr.readU32(ver))                                  { rdr.exitChunkPayload(chunk); return false; }
    if (ver != 2)                                           { rdr.exitChunkPayload(chunk); return false; }

    // Identity / basic config
    uint8_t devU8 = 0;
    if (!rdr.readU8(devU8))                                 { rdr.exitChunkPayload(chunk); return false; }
    setDeviceNumber(static_cast<int>(devU8));

    // Disk attachment info
    bool savedDiskLoaded = false;
    bool savedWriteProtected = false;
    std::string savedDiskName;

    if (!rdr.readBool(savedDiskLoaded))                     { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(savedWriteProtected))                 { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readString(savedDiskName))                     { rdr.exitChunkPayload(chunk); return false; }

    // Drive configuration / status
    uint8_t savedSide = 0;
    uint8_t savedLastError = 0;
    uint8_t savedDriveStatus = 0;

    if (!rdr.readU8(savedSide))                             { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readU8(savedLastError))                        { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readU8(savedDriveStatus))                      { rdr.exitChunkPayload(chunk); return false; }

    // Mount or remove media before restoring CPU and chip state because
    // loadDisk() and resetForMediaChange() reset drive runtime state.
    if (savedDiskLoaded)
    {
        if (savedDiskName.empty())                          { rdr.exitChunkPayload(chunk); return false; }

        loadDisk(savedDiskName);

        if (!diskLoaded || !diskImage)                      { rdr.exitChunkPayload(chunk); return false; }
    }
    else
    {
        // Preserve any pending writes from the currently mounted image.
        flushAndSaveDisk();

        resetForMediaChange();

        diskImage.reset();
        loadedDiskName.clear();
        diskLoaded = false;
        diskWriteProtected = false;
    }

    // Restore authoritative media and drive-status values
    diskLoaded = savedDiskLoaded;
    diskWriteProtected = savedWriteProtected;
    loadedDiskName = savedDiskLoaded ? savedDiskName : std::string{};

    currentSide = savedSide;
    lastError = static_cast<DriveError>(savedLastError);
    currentDriveStatus = static_cast<DriveStatus>(savedDriveStatus);

    // CPU state
    if (!driveCPU.loadStatePayload(rdr))                    { rdr.exitChunkPayload(chunk); return false; }
    if (!driveCPU.loadStateExtendedPayload(chunk, rdr))     { rdr.exitChunkPayload(chunk); return false; }

    // Mechanics / runtime state
    if (!rdr.readBool(motorOn))                             { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readU8(currentTrack))                          { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readU8(currentSector))                         { rdr.exitChunkPayload(chunk); return false; }

    // IEC protocol + line levels
    if (!rdr.readBool(atnLineLow))                          { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(clkLineLow))                          { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(dataLineLow))                         { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(srqAsserted))                         { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readBool(iecLinesPrimed))                      { rdr.exitChunkPayload(chunk); return false; }

    // Memory + chips
    if (!d1581mem.loadState(rdr))                           { rdr.exitChunkPayload(chunk); return false; }
    if (!d1581mem.getCIA().loadState(rdr))                  { rdr.exitChunkPayload(chunk); return false; }
    if (!d1581mem.getFDC().loadState(rdr))                  { rdr.exitChunkPayload(chunk); return false; }

    rdr.exitChunkPayload(chunk);

    // Post-restore fixups

    // Ensure the FDC remains configured for 1581 physical sectors
    d1581mem.getFDC().setSectorSize(512);

    // Synchronize CIA inputs with the restored global IEC bus state
    forceSyncIEC();

    // Restore the drive's SRQ output
    peripheralAssertSrq(srqAsserted);

    // IRQ is derived from the restored CIA and FDC states
    updateIRQ();

    // UI state is derived rather than serialized
    uiTrack = currentTrack;
    uiSector = currentSector;
    uiLedWasOn = false;
    powerLedOn = true;
    activityLedOn = false;
    activityPulseFrames = 0;

    return true;
}

void D1581::reset()
{
    motorOn = false;

    // Status
    lastError           = DriveError::NONE;
    currentDriveStatus  = DriveStatus::IDLE;
    currentTrack        = 17;
    currentSector       = 0;

    // IEC BUS line state
    atnLineLow          = false;
    clkLineLow          = false;
    dataLineLow         = false;
    srqAsserted         = false;

    // IEC communication
    iecLinesPrimed      = false;

    // Drive/Peripheral protocol state
    listening = false;
    talking   = false;

    currentSecondaryAddress = 0xFF;

    shiftReg      = 0;
    bitsProcessed = 0;

    waitingForAck               = false;
    ackEdgeCountdown            = 0;
    swallowPostHandshakeFalling = false;
    waitingForClkRelease        = false;
    prevClkLevel                = true;
    ackHold                     = false;
    byteAckHold                 = false;
    ackDelay                    = 0;

    while (!talkQueue.empty())
        talkQueue.pop();

    currentDriveBusState = DriveBusState::IDLE;

    // UI activity
    uiTrack       = currentTrack;
    uiSector      = currentSector;
    uiLedWasOn    = false;
    powerLedOn    = true;
    activityLedOn = false;

    // Release actual IEC outputs
    peripheralAssertClk(false);
    peripheralAssertData(false);
    peripheralAssertSrq(false);

    if (iecBus)
    {
        iecBus->unTalk(deviceNumber);
        iecBus->unListen(deviceNumber);
    }

    // Set correct sector size for D81.
    d1581mem.getFDC().setSectorSize(512);

    d1581mem.reset();
    driveCPU.reset();

    forceSyncIEC();
    updateIRQ();
}

void D1581::tick(uint32_t cycles)
{
    while (cycles-- > 0)
    {
        // CPU::tick() is already one external CPU cycle.
        driveCPU.tick();

        // Match D1571 behavior: local chips advance one cycle,
        // not getElapsedCycles() cycles.
        d1581mem.tick(1);

        updateIRQ();

        Drive::tick(1);

        const bool visibleActivity = activityLedOn || activityPulseFrames > 0;

        if (visibleActivity)
        {
            uiTrack  = currentTrack;
            uiSector = currentSector;
        }

        uiLedWasOn = visibleActivity;
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
    flushAndSaveDisk();

    diskImage.reset();
    diskLoaded = false;
    loadedDiskName.clear();

    lastError          = DriveError::NO_DISK;
    currentDriveStatus = DriveStatus::IDLE;
}

void D1581::forceSyncIEC()
{
    if (!iecBus) return;

    const bool newAtnLow  = !iecBus->readAtnLine();
    const bool newClkLow  = !iecBus->readClkLine();
    const bool newDataLow = !iecBus->readDataLine();

    atnLineLow  = newAtnLow;
    clkLineLow  = newClkLow;
    dataLineLow = newDataLow;

    d1581mem.getCIA().primeAtnLevel(atnLineLow);
    d1581mem.getCIA().setIECInputs(atnLineLow, clkLineLow, dataLineLow, srqAsserted);
}

void D1581::atnChanged(bool atnLow)
{
    (void)atnLow;
    syncLiveIECInputs();
}

void D1581::clkChanged(bool clkLow)
{
    (void)clkLow;
    syncLiveIECInputs();
}

void D1581::dataChanged(bool dataLow)
{
    (void)dataLow;
    syncLiveIECInputs();
}

void D1581::onListen()
{
    listening = true;
    talking   = false;

    // After LISTEN, next byte is also a secondary address.
    currentSecondaryAddress = 0xFF;

    // Clear stale transfer/handshake state inherited from Peripheral/Drive.
    shiftReg      = 0;
    bitsProcessed = 0;

    waitingForAck               = false;
    ackEdgeCountdown            = 0;
    swallowPostHandshakeFalling = false;
    waitingForClkRelease        = false;
    prevClkLevel                = true;
    ackHold                     = false;
    byteAckHold                 = false;
    ackDelay                    = 0;

#ifdef Debug
    std::cout << "[D1581] onListen() device=" << int(deviceNumber)
              << " listening=1 talking=0\n";
#endif
}

void D1581::onUnListen()
{
    listening = false;

    shiftReg      = 0;
    bitsProcessed = 0;

    waitingForAck               = false;
    ackEdgeCountdown            = 0;
    swallowPostHandshakeFalling = false;
    waitingForClkRelease        = false;
    prevClkLevel                = true;
    ackHold                     = false;
    byteAckHold                 = false;
    ackDelay                    = 0;

    peripheralAssertData(false);
    peripheralAssertClk(false);
    peripheralAssertSrq(false);

    currentDriveBusState = DriveBusState::IDLE;
    currentDriveStatus   = DriveStatus::IDLE;
    activityLedOn        = false;

#ifdef Debug
    std::cout << "[D1581] onUnListen() device=" << int(deviceNumber) << "\n";
#endif
}

void D1581::onTalk()
{
    talking   = true;
    listening = false;

    // After TALK, the next byte from the C64 is a secondary address.
    currentSecondaryAddress = 0xFF;

    // Clear stale receive state.
    shiftReg      = 0;
    bitsProcessed = 0;

    waitingForAck               = false;
    ackEdgeCountdown            = 0;
    swallowPostHandshakeFalling = false;
    waitingForClkRelease        = false;
    prevClkLevel                = true;
    ackHold                     = false;
    byteAckHold                 = false;
    ackDelay                    = 0;

    peripheralAssertClk(false);

#ifdef Debug
    std::cout << "[D1581] onTalk() device=" << int(deviceNumber)
              << " talking=1 listening=0\n";
#endif
}

void D1581::onUnTalk()
{
    talking = false;

    shiftReg      = 0;
    bitsProcessed = 0;

    waitingForAck               = false;
    ackEdgeCountdown            = 0;
    swallowPostHandshakeFalling = false;
    waitingForClkRelease        = false;
    prevClkLevel                = true;
    ackHold                     = false;
    byteAckHold                 = false;
    ackDelay                    = 0;

    peripheralAssertData(false);
    peripheralAssertClk(false);
    peripheralAssertSrq(false);

    currentDriveBusState = DriveBusState::IDLE;
    currentDriveStatus   = DriveStatus::IDLE;
    activityLedOn        = false;

#ifdef Debug
    std::cout << "[D1581] onUnTalk() device=" << int(deviceNumber) << "\n";
#endif
}

void D1581::onSecondaryAddress(uint8_t sa)
{
    currentSecondaryAddress = sa;

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
        currentDriveStatus = DriveStatus::ERROR;
        return false;
    }

    currentDriveStatus = DriveStatus::READING;

    const uint16_t d81Track = mapFdcTrackToD81Track(track);

    uint8_t logicalSector0 = 0;
    uint8_t logicalSector1 = 0;

    if (!mapFdcSectorToD81Sectors(getCurrentSide() & 1,
                                  sector,
                                  logicalSector0,
                                  logicalSector1))
    {
        lastError = DriveError::BAD_SECTOR;
        currentDriveStatus = DriveStatus::ERROR;
        return false;
    }

    auto part0 = diskImage->readSector(static_cast<uint8_t>(d81Track), logicalSector0);
    auto part1 = diskImage->readSector(static_cast<uint8_t>(d81Track), logicalSector1);

    if (part0.empty() || part1.empty())
    {

        lastError = DriveError::BAD_SECTOR;
        currentDriveStatus = DriveStatus::ERROR;
        return false;
    }

    std::memset(buffer, 0x00, length);

    const size_t copy0 = std::min<size_t>(part0.size(), std::min<size_t>(256, length));
    std::memcpy(buffer, part0.data(), copy0);

    size_t copy1 = 0;

    if (length > 256)
    {
        copy1 = std::min<size_t>(part1.size(), std::min<size_t>(256, length - 256));
        std::memcpy(buffer + 256, part1.data(), copy1);
    }

    pulseDiskActivity(track, sector);

    lastError           = DriveError::NONE;
    currentDriveStatus  = DriveStatus::IDLE;

    return true;
}

bool D1581::fdcWriteSector(uint8_t track, uint8_t sector, const uint8_t* buffer, size_t length)
{
    if (!diskLoaded || !diskImage || !buffer || length == 0)
    {
        lastError = DriveError::NO_DISK;
        return false;
    }

    if (fdcIsWriteProtected())
    {
        return false;
    }

    const uint16_t d81Track = mapFdcTrackToD81Track(track);

    uint8_t logicalSector0 = 0;
    uint8_t logicalSector1 = 0;

    if (!mapFdcSectorToD81Sectors(getCurrentSide() & 1, sector, logicalSector0, logicalSector1))
    {
        lastError = DriveError::BAD_SECTOR;
        return false;
    }

    std::vector<uint8_t> part0(256, 0x00);
    std::vector<uint8_t> part1(256, 0x00);

    const size_t copy0 = std::min<size_t>(256, length);
    std::memcpy(part0.data(), buffer, copy0);

    size_t copy1 = 0;

    if (length > 256)
    {
        copy1 = std::min<size_t>(256, length - 256);
        std::memcpy(part1.data(), buffer + 256, copy1);
    }

    const bool ok0 = diskImage->writeSector(static_cast<uint8_t>(d81Track), logicalSector0, part0);
    const bool ok1 = diskImage->writeSector(static_cast<uint8_t>(d81Track), logicalSector1, part1);

    if (ok0 && ok1)
    {
        pulseDiskActivity(track, sector);

        lastError           = DriveError::NONE;
        currentDriveStatus  = DriveStatus::IDLE;
        return true;
    }

    lastError = DriveError::BAD_SECTOR;
    return false;
}

void D1581::loadDisk(const std::string& path)
{
    flushAndSaveDisk();

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

    resetForMediaChange();

    // Success load it
    diskImage           = std::move(img);
    diskLoaded          = true;
    loadedDiskName      = path;
    lastError           = DriveError::NONE;
    activityLedOn       = false;
    activityPulseFrames = 0;
    uiLedWasOn          = false;
    uiTrack             = currentTrack;
    uiSector            = currentSector;

    forceSyncIEC();
    updateIRQ();
}

uint16_t D1581::mapFdcTrackToD81Track(uint8_t fdcTrack) const
{
    // Physical FDC cylinder 0..79 maps to D81 logical track 1..80.
    return uint16_t(fdcTrack) + 1;
}

bool D1581::mapFdcSectorToD81Sectors(uint8_t side,
                                     uint8_t fdcSector,
                                     uint8_t& logicalSector0,
                                     uint8_t& logicalSector1) const
{
    // A 1581 uses 10 physical MFM sectors per track (1-10)
    if (fdcSector < 1 || fdcSector > 10)
        return false;

    uint8_t base = static_cast<uint8_t>((fdcSector - 1) * 2);

    // If reading Side 1, shift into the upper 20 logical sectors of the D81 track
    if (side != 0)
    {
        base += 20;
    }

    logicalSector0 = base;
    logicalSector1 = static_cast<uint8_t>(base + 1);
    return true;
}

void D1581::getDriveIndicators(std::vector<Indicator>& out) const
{
    out.clear();

    Indicator pwr;
    pwr.name = "PWR";
    pwr.on = isPowerLedOn();
    pwr.color = IDriveIndicatorView::DriveIndicatorColor::Red;
    out.push_back(std::move(pwr));

    Indicator act;
    act.name = "ACT";
    act.on = activityLedOn || activityPulseFrames > 0 || talking || listening;
    act.color = IDriveIndicatorView::DriveIndicatorColor::Green;
    out.push_back(std::move(act));

    if (activityPulseFrames > 0)
        --activityPulseFrames;
}

void D1581::flushAndSaveDisk()
{
    if (!diskLoaded || !diskImage)
        return;

    if (loadedDiskName.empty())
        return;

    if (!diskImage->isDirty())
        return;

    if (diskImage->saveDisk(loadedDiskName))
    {
        diskImage->clearDirty();

#ifdef Debug
        std::cout << "[D1581] Saved dirty disk image: "
                  << loadedDiskName
                  << "\n";
#endif
    }
    else
    {
#ifdef Debug
        std::cerr << "[D1581] Failed to save dirty disk image: "
                  << loadedDiskName
                  << "\n";
#endif
    }
}

void D1581::pulseDiskActivity(uint8_t track, uint8_t sector)
{
    currentTrack  = track;
    currentSector = sector;

    uiTrack  = track;
    uiSector = sector;

    // UI poll frames, not emulated drive cycles.
    // 80 gives a visible activity lamp during 1581 loads.
    activityPulseFrames = 80;
}

void D1581::resetForMediaChange()
{
    // IEC line levels
    atnLineLow      = false;
    clkLineLow      = false;
    dataLineLow     = false;
    srqAsserted     = false;
    iecLinesPrimed  = false;

    // Drive/Peripheral protocol state
    listening = false;
    talking   = false;

    currentSecondaryAddress = 0xFF;

    shiftReg      = 0;
    bitsProcessed = 0;

    waitingForAck               = false;
    ackEdgeCountdown            = 0;
    swallowPostHandshakeFalling = false;
    waitingForClkRelease        = false;
    prevClkLevel                = true;
    ackHold                     = false;
    byteAckHold                 = false;
    ackDelay                    = 0;

    while (!talkQueue.empty())
        talkQueue.pop();

    currentDriveBusState        = DriveBusState::IDLE;
    currentDriveStatus          = DriveStatus::IDLE;
    lastError                   = DriveError::NONE;

    // Runtime/UI
    activityLedOn               = false;
    activityPulseFrames         = 0;
    uiLedWasOn                  = false;
    uiTrack                     = currentTrack;
    uiSector                    = currentSector;

    // Release actual IEC outputs
    peripheralAssertClk(false);
    peripheralAssertData(false);
    peripheralAssertSrq(false);

    if (iecBus)
    {
        iecBus->unTalk(deviceNumber);
        iecBus->unListen(deviceNumber);
    }

    // Reset 1581-local chips/CPU for a clean media transition.
    d1581mem.getFDC().setSectorSize(512);
    d1581mem.reset();
    driveCPU.reset();

    forceSyncIEC();
    updateIRQ();
}

void D1581::syncLiveIECInputs()
{
    if (!iecBus)
        return;

    atnLineLow  = !iecBus->readAtnLine();
    clkLineLow  = !iecBus->readClkLine();
    dataLineLow = !iecBus->readDataLine();
    srqAsserted = !iecBus->readSrqLine();

    d1581mem.getCIA().setIECInputs(atnLineLow, clkLineLow, dataLineLow, srqAsserted);
}
