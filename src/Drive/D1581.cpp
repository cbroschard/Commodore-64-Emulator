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
    wrtr.writeU8(static_cast<uint8_t>(status));
    wrtr.writeU8(currentTrack);
    wrtr.writeU8(currentSector);

    // IEC protocol + line levels
    wrtr.writeBool(atnLineLow);
    wrtr.writeBool(clkLineLow);
    wrtr.writeBool(dataLineLow);
    wrtr.writeBool(srqAsserted);

    wrtr.writeBool(iecLinesPrimed);
    wrtr.writeBool(iecListening);
    wrtr.writeBool(iecTalking);

    wrtr.writeBool(expectingSecAddr);
    wrtr.writeBool(expectingDataByte);

    wrtr.writeU8(currentListenSA);
    wrtr.writeU8(currentTalkSA);

    // RX shifter
    wrtr.writeBool(iecRxActive);
    wrtr.writeU32(static_cast<uint32_t>(iecRxBitCount));
    wrtr.writeU8(iecRxByte);

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
    status = static_cast<DriveStatus>(tmp8);

    if (!rdr.readU8(currentTrack)) return false;
    if (!rdr.readU8(currentSector)) return false;

    // IEC protocol + line levels
    if (!rdr.readBool(atnLineLow)) return false;
    if (!rdr.readBool(clkLineLow)) return false;
    if (!rdr.readBool(dataLineLow)) return false;
    if (!rdr.readBool(srqAsserted)) return false;

    if (!rdr.readBool(iecLinesPrimed)) return false;
    if (!rdr.readBool(iecListening)) return false;
    if (!rdr.readBool(iecTalking)) return false;

    if (!rdr.readBool(expectingSecAddr)) return false;
    if (!rdr.readBool(expectingDataByte)) return false;

    if (!rdr.readU8(currentListenSA)) return false;
    if (!rdr.readU8(currentTalkSA)) return false;

    if (!rdr.readBool(iecRxActive)) return false;

    uint32_t tmp32 = 0;
    if (!rdr.readU32(tmp32)) return false;
    iecRxBitCount = static_cast<int>(tmp32);

    if (!rdr.readU8(iecRxByte)) return false;

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

    // Set correct sector size for d81
    d1581mem.getFDC().setSectorSize(512);

    d1581mem.reset();
    driveCPU.reset();

    d1581mem.getCIA().enableAutoAtnAck(true);

    forceSyncIEC();
}

void D1581::tick(uint32_t cycles)
{
    if (!iecLinesPrimed)
    {
        d1581mem.getCIA().primeAtnLevel(atnLineLow);
        iecLinesPrimed = true;
    }

    while (cycles > 0)
    {
        if (bus)
        {
            const bool newAtnLow  = !bus->readAtnLine();
            const bool newClkLow  = !bus->readClkLine();
            const bool newDataLow = !bus->readDataLine();

            if (newAtnLow  != atnLineLow)  atnChanged(newAtnLow);
            if (newClkLow  != clkLineLow)  clkChanged(newClkLow);
            if (newDataLow != dataLineLow) dataChanged(newDataLow);
        }

        driveCPU.tick();
        uint32_t dc = driveCPU.getElapsedCycles();
        if (dc == 0) dc = 1;

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
    if (!bus) return;

    const bool newAtnLow  = !bus->readAtnLine();
    const bool newClkLow  = !bus->readClkLine();
    const bool newDataLow = !bus->readDataLine();

    atnLineLow  = newAtnLow;
    clkLineLow  = newClkLow;
    dataLineLow = newDataLow;

    // Update the CIA
    d1581mem.getCIA().setIECInputs(newAtnLow, newClkLow, newDataLow);
    d1581mem.getCIA().setFlagLine(!newAtnLow);
    d1581mem.getCIA().linesChanged();
}

void D1581::atnChanged(bool atnLow)
{
    if (atnLow == atnLineLow) return;

    atnLineLow = atnLow;

    // Keep CIA informed
    d1581mem.getCIA().setIECInputs(atnLineLow, clkLineLow, dataLineLow);
    d1581mem.getCIA().setFlagLine(!atnLow);
    d1581mem.getCIA().linesChanged();
}

void D1581::clkChanged(bool clkLow)
{
    if (clkLow == clkLineLow) return; // ignore no change

    clkLineLow = clkLow;

    d1581mem.getCIA().setIECInputs(atnLineLow, clkLineLow, dataLineLow);
    d1581mem.getCIA().linesChanged();
}

void D1581::dataChanged(bool dataLow)
{
    if (dataLow == dataLineLow) return; // ignore no change

    dataLineLow = dataLow;

    d1581mem.getCIA().setIECInputs(atnLineLow, clkLineLow, dataLineLow);
    d1581mem.getCIA().linesChanged();
}

void D1581::onListen()
{
    iecListening = true;
    iecTalking   = false;

    listening = true;
    talking   = false;

    // After LISTEN, next byte is also a secondary address
    expectingSecAddr  = true;
    expectingDataByte = false;
    currentSecondaryAddress = 0xFF;

    iecRxActive = false;
    iecRxBitCount = 0;
    iecRxByte = 0;
}

void D1581::onUnListen()
{
    iecListening = false;
    listening    = false;

    expectingSecAddr  = false;
    expectingDataByte = false;

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
