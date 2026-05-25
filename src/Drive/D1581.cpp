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
    activityLedOn(false)
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

void D1581::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("D158");

    // Version
    wrtr.writeU32(1);

    // Identity / basic config
    wrtr.writeU8(static_cast<uint8_t>(deviceNumber));
    wrtr.writeU8(currentSide);

    // CPU state (nested chunks/payload)
    driveCPU.saveStatePayload(wrtr);
    driveCPU.saveStateExtendedPayload(wrtr);

    // Mechanics / runtime state
    wrtr.writeBool(motorOn);

    // Disk attachment flags
    wrtr.writeBool(diskLoaded);
    wrtr.writeBool(diskWriteProtected);
    wrtr.writeString(loadedDiskName);

    // Status
    wrtr.writeU8(static_cast<uint8_t>(lastError));
    wrtr.writeU8(static_cast<uint8_t>(currentDriveStatus));
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
    if (!rdr.readU32(ver)) return false;
    if (ver != 1) return false;

    // Identity / basic config
    uint8_t devU8 = 0;
    if (!rdr.readU8(devU8)) return false;
    setDeviceNumber(static_cast<int>(devU8));

    if (!rdr.readU8(currentSide)) return false;

    // CPU state (nested chunks)
    StateReader::Chunk sub{};

    if (!rdr.nextChunk(sub)) return false;
    if (!driveCPU.loadStatePayload(rdr)) return false;

    if (!rdr.nextChunk(sub)) return false;
    if (!driveCPU.loadStateExtendedPayload(sub, rdr)) return false;

    // Mechanics / runtime state
    if (!rdr.readBool(motorOn)) return false;

    if (!rdr.readBool(diskLoaded)) return false;
    if (!rdr.readBool(diskWriteProtected)) return false;
    if (!rdr.readString(loadedDiskName)) return false;

    uint8_t tmp8 = 0;

    if (!rdr.readU8(tmp8)) return false;
    lastError = static_cast<DriveError>(tmp8);

    if (!rdr.readU8(tmp8)) return false;
    currentDriveStatus = static_cast<DriveStatus>(tmp8);

    if (!rdr.readU8(currentTrack)) return false;
    if (!rdr.readU8(currentSector)) return false;

    // IEC protocol + line levels
    if (!rdr.readBool(atnLineLow)) return false;
    if (!rdr.readBool(clkLineLow)) return false;
    if (!rdr.readBool(dataLineLow)) return false;
    if (!rdr.readBool(srqAsserted)) return false;

    if (!rdr.readBool(iecLinesPrimed)) return false;

    // Memory + chips
    if (!d1581mem.loadState(rdr)) return false;

    if (!d1581mem.getCIA().loadState(rdr)) return false;
    if (!d1581mem.getFDC().loadState(rdr)) return false;

    // Post-restore fixups

    // Ensure VIA/CIA sees current bus input levels immediately
    forceSyncIEC();

    // SRQ line output state
    peripheralAssertSrq(srqAsserted);

    // IRQ derived from chip IRQ sources
    updateIRQ();

    return true;
}

void D1581::reset()
{
    motorOn             = false;

    // Status
    lastError           = DriveError::NONE;
    currentDriveStatus       = DriveStatus::IDLE;
    currentTrack        = 17;
    currentSector       = 0;

    // IEC BUS reset
    atnLineLow          = false;
    clkLineLow          = false;
    dataLineLow         = false;
    srqAsserted         = false;

    // IEC Communication
    iecLinesPrimed      = false;

    // UI activity
    uiTrack             = currentTrack;
    uiSector            = currentSector;
    uiLedWasOn          = false;
    powerLedOn          = true;
    activityLedOn       = false;

    // Reset actual line states
    peripheralAssertClk(false);  // Release Clock
    peripheralAssertData(false); // Release Data
    peripheralAssertSrq(false);  // Release SRQ

    if (bus)
    {
        bus->unTalk(deviceNumber);
        bus->unListen(deviceNumber);
    }

    // Set correct sector size for d81
    d1581mem.getFDC().setSectorSize(512);

    d1581mem.reset();
    driveCPU.reset();

    forceSyncIEC();
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

        const bool ledOn = activityLedOn;

        if (ledOn)
            uiTrack = currentTrack;

        if (ledOn && !uiLedWasOn)
            uiSector = currentSector;

        uiLedWasOn = ledOn;
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
    if (!bus) return;

    const bool newAtnLow  = !bus->readAtnLine();
    const bool newClkLow  = !bus->readClkLine();
    const bool newDataLow = !bus->readDataLine();

    atnLineLow  = newAtnLow;
    clkLineLow  = newClkLow;
    dataLineLow = newDataLow;

    d1581mem.getCIA().setIECInputs(atnLineLow, clkLineLow, dataLineLow, srqAsserted);
    d1581mem.getCIA().primeAtnLevel(atnLineLow);
}

void D1581::atnChanged(bool atnLow)
{
    if (atnLow == atnLineLow) return;

    atnLineLow = atnLow;
    const bool newSrqLow = bus ? !bus->readSrqLine() : srqAsserted;
    d1581mem.getCIA().setIECInputs(atnLineLow, clkLineLow, dataLineLow, newSrqLow);
}

void D1581::clkChanged(bool clkLow)
{
    if (clkLow == clkLineLow) return;

    clkLineLow = clkLow;
    const bool newSrqLow = bus ? !bus->readSrqLine() : srqAsserted;
    d1581mem.getCIA().setIECInputs(atnLineLow, clkLineLow, dataLineLow, newSrqLow);
}

void D1581::dataChanged(bool dataLow)
{
    if (dataLow == dataLineLow) return;

    dataLineLow = dataLow;
    const bool newSrqLow = bus ? !bus->readSrqLine() : srqAsserted;
    d1581mem.getCIA().setIECInputs(atnLineLow, clkLineLow, dataLineLow, newSrqLow);
}

void D1581::onListen()
{
    listening = true;
    talking   = false;

    // After LISTEN, next byte is also a secondary address
    currentSecondaryAddress = 0xFF;
}

void D1581::onUnListen()
{
    listening = false;

    peripheralAssertData(false);
    peripheralAssertClk(false);
    peripheralAssertSrq(false);

    forceSyncIEC();

    currentDriveStatus = DriveStatus::IDLE;
    activityLedOn = false;
}

void D1581::onTalk()
{
    talking   = true;
    listening = false;

    // After TALK, the next byte from the C64 is a secondary address
    currentSecondaryAddress = 0xFF;

    peripheralAssertClk(false);
}

void D1581::onUnTalk()
{
    talking = false;

    peripheralAssertData(false);
    peripheralAssertClk(false);
    peripheralAssertSrq(false);

    forceSyncIEC();

    currentDriveStatus = DriveStatus::IDLE;
    activityLedOn = false;
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
        activityLedOn = false;
        return false;
    }

    activityLedOn = true;
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
        activityLedOn = false;
        return false;
    }

    auto part0 = diskImage->readSector(static_cast<uint8_t>(d81Track), logicalSector0);
    auto part1 = diskImage->readSector(static_cast<uint8_t>(d81Track), logicalSector1);

    if (part0.empty() || part1.empty())
    {
        lastError = DriveError::BAD_SECTOR;
        currentDriveStatus = DriveStatus::ERROR;
        activityLedOn = false;
        return false;
    }

    std::memset(buffer, 0x00, length);

    const size_t copy0 = std::min<size_t>(part0.size(), std::min<size_t>(256, length));
    std::memcpy(buffer, part0.data(), copy0);

    if (length > 256)
    {
        const size_t copy1 = std::min<size_t>(part1.size(), std::min<size_t>(256, length - 256));
        std::memcpy(buffer + 256, part1.data(), copy1);
    }

    currentTrack  = track;
    currentSector = sector;
    uiTrack       = track;
    uiSector      = sector;
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

    if (fdcIsWriteProtected())
        return false;

    const uint16_t d81Track = mapFdcTrackToD81Track(track);

    uint8_t logicalSector0 = 0;
    uint8_t logicalSector1 = 0;

    if (!mapFdcSectorToD81Sectors(getCurrentSide() & 1,
                                  sector,
                                  logicalSector0,
                                  logicalSector1))
    {
        lastError = DriveError::BAD_SECTOR;
        return false;
    }

    std::vector<uint8_t> part0(256, 0x00);
    std::vector<uint8_t> part1(256, 0x00);

    const size_t copy0 = std::min<size_t>(256, length);
    std::memcpy(part0.data(), buffer, copy0);

    if (length > 256)
    {
        const size_t copy1 = std::min<size_t>(256, length - 256);
        std::memcpy(part1.data(), buffer + 256, copy1);
    }

    const bool ok0 = diskImage->writeSector(static_cast<uint8_t>(d81Track), logicalSector0, part0);
    const bool ok1 = diskImage->writeSector(static_cast<uint8_t>(d81Track), logicalSector1, part1);

    return ok0 && ok1;
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
    diskImage       = std::move(img);
    diskLoaded      = true;
    loadedDiskName  = path;
    lastError       = DriveError::NONE;
    activityLedOn   = false;
    uiLedWasOn      = false;
    uiTrack         = currentTrack;
    uiSector        = currentSector;

    forceSyncIEC();
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
    (void)side;

    // 1581 FDC sector IDs are treated as 1-based here.
    //
    // One physical FDC sector is 512 bytes.
    // One D81 logical sector is 256 bytes.
    //
    // FDC sector 1  -> D81 sectors 0,1
    // FDC sector 2  -> D81 sectors 2,3
    // ...
    // FDC sector 20 -> D81 sectors 38,39
    if (fdcSector < 1 || fdcSector > 20)
        return false;

    const uint8_t base = static_cast<uint8_t>((fdcSector - 1) * 2);

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
    act.on = isActivityLedOn();
    act.color = IDriveIndicatorView::DriveIndicatorColor::Green;
    out.push_back(std::move(act));
}

void D1581::flushAndSaveDisk()
{
    // todo: add code to save to disk
}
