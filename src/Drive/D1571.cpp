// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "CPUBus.h"
#include "Drive/D1571Bus.h"
#include "Drive/D1571.h"

D1571::D1571(int deviceNumber, const std::string& romName) :
    motorOn(false),
    mediaPath(MediaPath::GCR_D64),
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
    currentSide(0),
    busDriversEnabled(false),
    twoMHzMode(false),
    iecRxActive(false),
    iecRxBitCount(0),
    iecRxByte(0),
    diskLoaded(false),
    diskWriteProtected(false),
    halfTrackPos(17 * 2),
    currentTrack(17),
    currentSector(0),
    gcrBitCounter(0),
    gcrPos(0),
    gcrDirty(true),
    uiTrack(17),
    uiSector(0),
    uiLedWasOn(false)
{
    setDeviceNumber(deviceNumber);
    d1571mem.attachPeripheralInstance(this);
    d1571Bus.attachMemoryInstance(&d1571mem);
    driveCPU.attachCPUBusInstance(&d1571Bus);
    driveCPU.attachIRQLineInstance(&IRQ);

    if (!d1571mem.initialize(romName))
        throw std::runtime_error("Unable to start drive, ROM not loaded!\n");

    reset();
}

D1571::~D1571() = default;

void D1571::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("D157");
    wrtr.writeU32(2);
    wrtr.writeU8(static_cast<uint8_t>(deviceNumber));

    // Disk attachment info must come before CPU/chip state
    wrtr.writeBool(diskLoaded);
    wrtr.writeBool(diskWriteProtected);
    wrtr.writeString(loadedDiskName);

    wrtr.writeU8(static_cast<uint8_t>(mediaPath));
    wrtr.writeU8(static_cast<uint8_t>(lastError));
    wrtr.writeU8(static_cast<uint8_t>(status));

    // Dump the CPU state
    driveCPU.saveStatePayload(wrtr);
    driveCPU.saveStateExtendedPayload(wrtr);

    // Mechanics / runtime state
    wrtr.writeBool(motorOn);

    wrtr.writeU8(currentTrack);
    wrtr.writeU8(currentSector);
    wrtr.writeU8(densityCode);

    wrtr.writeBool(currentSide);
    wrtr.writeI32(halfTrackPos);

    // Protocol state
    wrtr.writeBool(iecListening);
    wrtr.writeBool(iecTalking);

    wrtr.writeBool(presenceAckDone);
    wrtr.writeBool(expectingSecAddr);
    wrtr.writeBool(expectingDataByte);

    wrtr.writeU8(currentListenSA);
    wrtr.writeU8(currentTalkSA);

    wrtr.writeI32(currentSecondaryAddress);

    // Receive shifter
    wrtr.writeBool(iecRxActive);
    wrtr.writeI32(iecRxBitCount);
    wrtr.writeU8(iecRxByte);

    // 1571 runtime flags
    wrtr.writeBool(busDriversEnabled);
    wrtr.writeBool(twoMHzMode);

    // IEC Bus line levels
    wrtr.writeBool(atnLineLow);
    wrtr.writeBool(clkLineLow);
    wrtr.writeBool(dataLineLow);
    wrtr.writeBool(srqAsserted);

    // GCR resume state
    wrtr.writeI32(gcrBitCounter);
    wrtr.writeU32(static_cast<uint32_t>(gcrPos));

    // UI Activity State
    wrtr.writeU8(uiTrack);
    wrtr.writeU8(uiSector);

    // Dump RAM
    d1571mem.saveState(wrtr);

    // Dump VIA1
    d1571mem.getVIA1().saveState(wrtr);

    // Dump VIA2
    d1571mem.getVIA2().saveState(wrtr);

    // Dump FDC
    d1571mem.getFDC().saveState(wrtr);

    wrtr.endChunk();
}

bool D1571::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    // Not our chunk
    if (std::memcmp(chunk.tag, "D157", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    // Header / identity
    uint32_t ver = 0;
    if (!rdr.readU32(ver))                                  { rdr.exitChunkPayload(chunk); return false; }
    if (ver != 2)                                           { rdr.exitChunkPayload(chunk); return false; }

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

    uint8_t savedMediaPath = 0;
    uint8_t savedLastError = 0;
    uint8_t savedStatus = 0;

    if (!rdr.readU8(savedMediaPath))                        { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readU8(savedLastError))                        { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readU8(savedStatus))                           { rdr.exitChunkPayload(chunk); return false; }

    // Mount or remove media before restoring CPU and chip state because
    // loadDisk() and resetForMediaChange() modify drive runtime state.
    if (savedDiskLoaded)
    {
        if (savedDiskName.empty())                          { rdr.exitChunkPayload(chunk); return false; }

        loadDisk(savedDiskName);

        if (!diskLoaded || !diskImage)                      { rdr.exitChunkPayload(chunk); return false; }
    }
    else
    {
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

    mediaPath = static_cast<MediaPath>(savedMediaPath);
    lastError = static_cast<DriveError>(savedLastError);
    status = static_cast<DriveStatus>(savedStatus);

    // CPU state (must match save order)
    if (!driveCPU.loadStatePayload(rdr))                    { rdr.exitChunkPayload(chunk); return false; }
    if (!driveCPU.loadStateExtendedPayload(chunk, rdr))     { rdr.exitChunkPayload(chunk); return false; }

    // Mechanics / runtime state
    if (!rdr.readBool(motorOn))                             { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readU8(currentTrack))                          { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readU8(currentSector))                         { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readU8(densityCode))                           { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readBool(currentSide))                         { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readI32(halfTrackPos))                         { rdr.exitChunkPayload(chunk); return false; }

    // IEC protocol state (D1571-local)
    if (!rdr.readBool(iecListening))                        { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(iecTalking))                          { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readBool(presenceAckDone))                     { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(expectingSecAddr))                    { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(expectingDataByte))                   { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readU8(currentListenSA))                       { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readU8(currentTalkSA))                         { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readI32(currentSecondaryAddress))              { rdr.exitChunkPayload(chunk); return false; }

    // Receive shifter
    if (!rdr.readBool(iecRxActive))                         { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readI32(iecRxBitCount))                        { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readU8(iecRxByte))                             { rdr.exitChunkPayload(chunk); return false; }

    // Runtime flags
    if (!rdr.readBool(busDriversEnabled))                   { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(twoMHzMode))                          { rdr.exitChunkPayload(chunk); return false; }

    // IEC bus line levels
    if (!rdr.readBool(atnLineLow))                          { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(clkLineLow))                          { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(dataLineLow))                         { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(srqAsserted))                         { rdr.exitChunkPayload(chunk); return false; }

    // GCR resume state
    uint32_t savedGcrPos = 0;

    if (!rdr.readI32(gcrBitCounter))                        { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readU32(savedGcrPos))                          { rdr.exitChunkPayload(chunk); return false; }

    // UI activity state
    if (!rdr.readU8(uiTrack))                               { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readU8(uiSector))                              { rdr.exitChunkPayload(chunk); return false; }

    if (!d1571mem.loadState(rdr))                           { rdr.exitChunkPayload(chunk); return false; }
    if (!d1571mem.getVIA1().loadState(rdr))                 { rdr.exitChunkPayload(chunk); return false; }
    if (!d1571mem.getVIA2().loadState(rdr))                 { rdr.exitChunkPayload(chunk); return false; }
    if (!d1571mem.getFDC().loadState(rdr))                  { rdr.exitChunkPayload(chunk); return false; }

    rdr.exitChunkPayload(chunk);

    // Post-restore fixups (IMPORTANT for deterministic resume)
    invalidateRawGcrCache();
    gcrPos = static_cast<size_t>(savedGcrPos);
    gcrDirty = true;

    // Sync VIA1 input pins to the bus line levels with the global restored IEC Bus state
    forceSyncIEC();

    // Bring SRQ output in sync with restored state
    peripheralAssertSrq(srqAsserted);

    // IRQ line derived from VIA/CIA/FDC state
    updateIRQ();

    // Derived from the restored VIA2 state
    uiLedWasOn = d1571mem.getVIA2().isLedOn();

    return true;
}

void D1571::tick(uint32_t cycles)
{
    while (cycles > 0)
    {
        if (checkBreakpoint())
            return;

        // One host/world cycle.
        const uint32_t cpuTicks = twoMHzMode ? 2u : 1u;

        for (uint32_t i = 0; i < cpuTicks; ++i)
        {
            driveCPU.tick();
            d1571mem.tick(1);
            updateIRQ();
        }

        // Disk rotation / GCR stream must stay at normal physical speed.
        if (isGCRMode() && motorOn && diskLoaded)
            gcrAdvance(1);

        const bool ledOn = d1571mem.getVIA2().isLedOn();

        if (ledOn)
            uiTrack = currentTrack;

        if (ledOn && !uiLedWasOn)
            uiSector = currentSector;

        uiLedWasOn = ledOn;

        --cycles;
    }
}

bool D1571::gcrTick()
{
    if (gcrDirty)
    {
        size_t oldPos = gcrPos;

        loadCurrentRawTrackFromCacheOrBuild();

        gcrDirty = false;

        if (!gcrTrack.empty())
            gcrPos = oldPos % gcrTrack.size();
        else
            gcrPos = 0;
    }

    if (gcrTrack.empty()) return false;

    if (gcrTrack.getSyncMap().size() != gcrTrack.size())
        gcrTrack.getSyncMap().assign(gcrTrack.size(), 0);

    const size_t pos = gcrPos;

    if (diskWriteGate)
    {
        pendingWritePos = pos;
        pendingWritePosValid = true;

        auto& via2 = d1571mem.getVIA2();
        via2.pulseWriteByteReady();

        gcrPos = (gcrPos + 1) % gcrTrack.size();
        return true;
    }

    uint8_t gcrByte = gcrTrack.getTrackData()[pos];
    bool syncHigh = (gcrTrack.getSyncMap()[pos] != 0);

    if (!getG64Image())
    {
        if (gcrSectorAtPos.size() == gcrTrack.size())
            currentSector = gcrSectorAtPos[pos];
    }
    else
        sampleHeaderAtCurrentPosition(pos);

    gcrPos = (gcrPos + 1) % gcrTrack.size();

    d1571mem.getVIA2().diskByteFromMedia(gcrByte, syncHigh);

    return true;
}

void D1571::gcrAdvance(uint32_t dc)
{
    gcrBitCounter += static_cast<int>(dc);

    while (true)
    {
        uint8_t activeDensity = densityCode;
        const auto& speedZones = gcrTrack.getSpeedZones();

        if (!speedZones.empty())
        {
            const size_t speedPos = gcrPos % speedZones.size();
            activeDensity = speedZones[speedPos] & 0x03;
        }

        const int cyclesPerByte = gcrCodec.cyclesPerByteFromDensity(activeDensity);

        if (gcrBitCounter < cyclesPerByte)
            break;

        gcrBitCounter -= cyclesPerByte;

        gcrTick();
    }
}

void D1571::reset()
{
    motorOn                     = false;
    diskWriteProtected          = false;
    lastError                   = DriveError::NONE;
    status                      = DriveStatus::IDLE;
    currentTrack                = 17;
    currentSector               = 0;
    densityCode                 = 2;

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
    currentListenSA             = 0;
    currentTalkSA               = 0;
    iecRxActive                 = false;
    iecRxBitCount               = 0;
    iecRxByte                   = 0;

    // 1571 Runtime Properties reset
    currentSide                 = 0;
    busDriversEnabled           = false;
    twoMHzMode                  = false;
    halfTrackPos                = currentTrack * 2;
    gcrBitCounter               = 0;
    gcrPos                      = 0;
    gcrDirty                    = true;

    // UI activity
    uiTrack                     = currentTrack;
    uiSector                    = currentSector;
    uiLedWasOn                  = false;

    // Reset actual line states
    peripheralAssertClk(false);  // Release Clock
    peripheralAssertData(false); // Release Data
    peripheralAssertSrq(false);  // Release SRQ

    if (iecBus)
    {
        iecBus->unTalk(deviceNumber);
        iecBus->unListen(deviceNumber);
    }

    gcrTrack.clear();
    gcrTrack.getSyncMap().clear();
    gcrSectorAtPos.clear();

    d1571mem.reset();
    driveCPU.reset();
}

void D1571VIA::resetShift()
{
    srShiftReg = 0;
    srBitCount = 0;
}

void D1571::setSRQAsserted(bool state)
{
    srqAsserted = state;
}

void D1571::forceSyncIEC()
{
    if (iecBus)
    {
        atnLineLow  = !iecBus->readAtnLine();
        clkLineLow  = !iecBus->readClkLine();
        dataLineLow = !iecBus->readDataLine();
    }

    auto& via1 = d1571mem.getVIA1();
    via1.setIECInputLines(atnLineLow, clkLineLow, dataLineLow);

    auto& cia = d1571mem.getCIA();
    cia.primeAtnLevel(atnLineLow);
    cia.setIECInputs(atnLineLow, clkLineLow, dataLineLow);
}

void D1571::setDensityCode(uint8_t code)
{
    uint8_t oldCode = densityCode;
    code &= 0x03;

    if (oldCode != code)
    {
        saveCurrentRawTrackToCache();

        densityCode = code;
        gcrDirty = true;
    }
}

void D1571::setHeadSide(bool side1)
{
    // D64 must remain single-sided.
    if (mediaPath == MediaPath::GCR_D64 || mediaPath == MediaPath::GCR_G64)
    {
        if (currentSide != 0)
        {
            saveCurrentRawTrackToCache();

            currentSide = 0;

            // Force rebuild, but preserve rotational position.
            gcrTrack.clear();
            gcrTrack.getSyncMap().clear();
            gcrSectorAtPos.clear();

            d1571mem.getVIA2().clearMechBytePending();

            gcrDirty = true;
        }

        return;
    }

    const bool newSide = side1 ? true : false;

    if (currentSide != newSide)
    {
        saveCurrentRawTrackToCache();

        currentSide = newSide;

        gcrTrack.clear();
        gcrTrack.getSyncMap().clear();
        gcrSectorAtPos.clear();

        d1571mem.getVIA2().clearMechBytePending();

        gcrDirty = true;
    }
}

void D1571::setBusDriversEnabled(bool output)
{
    busDriversEnabled = output;
}

void D1571::setBurstClock2MHz(bool enable)
{
    if (twoMHzMode == enable)
        return;

    twoMHzMode = enable;
}

bool D1571::getByteReadyLow() const
{
    if (isGCRMode())
        return d1571mem.getVIA2().mechHasBytePending();

    auto* fdc = getFDC();
    if (!fdc) return false;

    bool drqActive = fdc->checkDRQActive();
    bool intrqActive = fdc->checkIRQActive();
    return drqActive || intrqActive;
}

void D1571::rebuildGCRTrackStream()
{
    CBMImage* cbmImage = getCBMImage();

    if (!cbmImage)
        return;

    gcrTrack.clear();
    gcrTrack.getSyncMap().clear();
    gcrSectorAtPos.clear();

    if (!diskLoaded || !diskImage) return;

    const size_t cacheTrack = currentRawCacheIndex();

    if (cacheTrack < rawGcrTrackValid.size() && rawGcrTrackValid[cacheTrack])
    {
        gcrTrack.getTrackData() = rawGcrTrackCache[cacheTrack];
        gcrTrack.getSyncMap()        = rawGcrSyncCache[cacheTrack];
        gcrSectorAtPos = rawGcrSectorCache[cacheTrack];

        if (!gcrTrack.empty())
            gcrPos %= gcrTrack.size();
        else
            gcrPos = 0;

        d1571mem.getVIA2().clearMechBytePending();
        return;
    }

    const int trackOnSide1based = int(currentTrackOnSide1Based());
    const int imageTrack1based  = int(currentImageTrack1Based());
    const int spt = gcrCodec.sectorsPerTrack1541(trackOnSide1based);

    std::vector<uint8_t> bam = cbmImage->readSector(18, 0);
    if (bam.size() < 256)
        bam.resize(256, 0x00);

    uint8_t id1 = bam[0xA2];
    uint8_t id2 = bam[0xA3];

    auto pushN = [&](uint8_t v, int count, bool isSync, uint8_t sectorTag)
    {
        gcrTrack.getTrackData().insert(gcrTrack.getTrackData().end(), count, v);
        gcrTrack.getSyncMap().insert(gcrTrack.getSyncMap().end(), count, isSync ? 1 : 0);
        gcrSectorAtPos.insert(gcrSectorAtPos.end(), count, sectorTag);
    };

    auto pushEncoded = [&](const uint8_t* in, size_t len, uint8_t sectorTag)
    {
        for (size_t i = 0; i < len; i += 4)
        {
            uint8_t g[5];
            gcrCodec.encode4Bytes(&in[i], g);

            gcrTrack.getTrackData().insert(gcrTrack.getTrackData().end(), g, g + 5);
            gcrTrack.getSyncMap().insert(gcrTrack.getSyncMap().end(), 5, 0);
            gcrSectorAtPos.insert(gcrSectorAtPos.end(), 5, sectorTag);
        }
    };

    // CBM DOS-ish defaults
    constexpr int SYNC_LEN      = 10;   // $FF bytes in sync mode
    constexpr int HEADER_GAP    = 9;   //  $55 bytes
    constexpr int TAIL_GAP      = 9;   // typical 4..12 between sectors

    // lead-in gap (NOT sync)
    pushN(0x55, 64, false, 0);

    for (int sector = 0; sector < spt; ++sector)
    {
        const uint8_t sectorTag = static_cast<uint8_t>(sector);
        std::vector<uint8_t> sec = cbmImage->readSector(uint8_t(imageTrack1based), uint8_t(sectorTag));


        if (sec.size() != 256) sec.assign(256, 0x00);

        // ---- HEADER ----
        pushN(0xFF, SYNC_LEN, true, sectorTag);        // sync region

        uint8_t hdr[8] = {0};
        hdr[0] = 0x08;
        hdr[2] = uint8_t(sector);            // Byte 2 is Sector
        hdr[3] = uint8_t(imageTrack1based); // Physical track on selected side
        hdr[4] = id2;
        hdr[5] = id1;
        hdr[6] = 0x0F;
        hdr[7] = 0x0F;
        hdr[1] = uint8_t(hdr[2] ^ hdr[3] ^ hdr[4] ^ hdr[5]);

        pushEncoded(hdr, 8, sectorTag);
        pushN(0x55, HEADER_GAP, false, sectorTag);

        // ---- DATA ----
        pushN(0xFF, SYNC_LEN, true, sectorTag);

        std::vector<uint8_t> raw(260, 0x00);
        raw[0] = 0x07;          // data block ID

        uint8_t csum = 0;

        for (int i = 0; i < 256; ++i)
        {
            raw[1 + i] = sec[i];
            csum ^= raw[1 + i];
        }

        raw[257] = csum;        // checksum goes AFTER the 256 data bytes
        raw[258] = 0x00;
        raw[259] = 0x00;

        pushEncoded(raw.data(), raw.size(), sectorTag);
        pushN(0x55, TAIL_GAP, false, sectorTag);
    }

    pushN(0x55, 128, false, 0);

    // sanity
    if (gcrTrack.getSyncMap().size() != gcrTrack.size())
        gcrTrack.getSyncMap().assign(gcrTrack.size(), 0);

    if (gcrSectorAtPos.size() != gcrTrack.size())
        gcrSectorAtPos.assign(gcrTrack.size(), currentSector);

    if (!gcrTrack.empty())
        gcrPos %= gcrTrack.size();
    else
        gcrPos = 0;

    d1571mem.getVIA2().clearMechBytePending();
}

void D1571::syncTrackFromFDC()
{
    auto* fdc = getFDC();
    if (!fdc) return;

    currentTrack = fdc->getCurrentTrack();
}

void D1571::updateIRQ()
{
    bool via1IRQ = d1571mem.getVIA1().checkIRQActive();
    bool via2IRQ = d1571mem.getVIA2().checkIRQActive();
    bool ciaIRQ = d1571mem.getCIA().checkIRQActive();
    bool fdcIRQ = d1571mem.getFDC().checkIRQActive();

    bool any = via1IRQ || via2IRQ || ciaIRQ || fdcIRQ;

    if (any) IRQ.raiseIRQ(IRQLine::D1571_IRQ);
    else IRQ.clearIRQ(IRQLine::D1571_IRQ);
}

void D1571::onStepperPhaseChange(uint8_t oldPhase, uint8_t newPhase)
{
    oldPhase &= 0x03;
    newPhase &= 0x03;

    if (oldPhase == newPhase)
        return;

    int step = 0;

    if (((oldPhase + 1) & 0x03) == newPhase)
        step = +1;
    else if (((oldPhase + 3) & 0x03) == newPhase)
        step = -1;
    else
        return; // ignore illegal two-phase jump

    saveCurrentRawTrackToCache();

    int maxHalfTrack = 34 * 2;

    if (const G64* g64Image = getG64Image())
    {
        if (g64Image->getTrackCount() > 0)
            maxHalfTrack = static_cast<int>(g64Image->getTrackCount()) - 1;
    }

    halfTrackPos = std::clamp(halfTrackPos + step, 0, maxHalfTrack);

    currentTrack = static_cast<uint8_t>(halfTrackPos / 2);

    uiTrack = currentTrack;
    uiSector = currentSector;

    gcrDirty = true;
}

void D1571::loadDisk(const std::string& path)
{
    flushAndSaveDisk();

    resetForMediaChange();


    auto img = DiskFactory::create(path);

    if (!img)
    {
        loadedDiskName.clear();
        diskImage.reset();
        diskLoaded = false;
        lastError = DriveError::NO_DISK;

        gcrDirty = true;
        gcrPos = 0;
        gcrBitCounter = 0;

        gcrTrack.clear();
        gcrTrack.getSyncMap().clear();
        gcrSectorAtPos.clear();

        d1571mem.getVIA2().clearMechBytePending();

        return;
    }

    if (!img->loadDisk(path))
    {
        loadedDiskName.clear();
        diskImage.reset();
        diskLoaded = false;
        lastError = DriveError::NO_DISK;

        gcrDirty = true;
        gcrPos = 0;
        gcrBitCounter = 0;

        gcrTrack.clear();
        gcrTrack.getSyncMap().clear();
        gcrSectorAtPos.clear();

        d1571mem.getVIA2().clearMechBytePending();

        return;
    }

    diskImage = std::move(img);

    diskWriteProtected = diskImage->isWriteProtected();

    // Determine media path from actual image type.
    if (getG64Image())
        mediaPath = MediaPath::GCR_G64;
    else
    {
        auto lowerExt = [](const std::string& p) -> std::string
        {
            const auto dot = p.find_last_of('.');

            if (dot == std::string::npos)
                return {};

            std::string ext = p.substr(dot);

            for (auto& c : ext)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

            return ext;
        };

        const std::string ext = lowerExt(path);

        if (ext == ".d71")
            mediaPath = MediaPath::GCR_D71;
        else if (ext == ".d64")
            mediaPath = MediaPath::GCR_D64;
        else
            mediaPath = MediaPath::FDC_MFM;
    }

    diskLoaded = true;

    invalidateRawGcrCache();

    loadedDiskName = path;
    status = DriveStatus::READY;
    lastError = DriveError::NONE;

    gcrDirty = true;
    gcrPos = 0;
    gcrBitCounter = 0;

    gcrTrack.clear();
    gcrTrack.getSyncMap().clear();
    gcrSectorAtPos.clear();

    d1571mem.getVIA2().clearMechBytePending();
}

void D1571::unloadDisk()
{
    flushAndSaveDisk();

    // Drop the current image
    diskWriteProtected = false;
    diskImage.reset();
    diskLoaded = false;
    loadedDiskName.clear();

    // Reset basic geometry/status
    currentTrack        = 17;
    currentSector       = 0;
    uiTrack             = currentTrack;
    uiSector            = currentSector;
    lastError           = DriveError::NONE;
    status              = DriveStatus::IDLE;
}

bool D1571::fdcReadSector(uint8_t track, uint8_t sector, uint8_t* buffer, size_t length)
{
    CBMImage* cbmImage = getCBMImage();

    if (!diskLoaded || !cbmImage || buffer == nullptr || length == 0)
    {
        lastError = DriveError::NO_DISK;
        return false;
    }

    std::vector<uint8_t> data = cbmImage->readSector(track, sector);

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
    CBMImage* cbmImage = getCBMImage();

    if (!diskLoaded || !cbmImage || buffer == nullptr || length == 0)
    {
        lastError = DriveError::NO_DISK;
        return false;
    }

    constexpr size_t SECTOR_SIZE = 256;

    const size_t toCopy = std::min(length, SECTOR_SIZE);

    std::vector<uint8_t> data;
    data.assign(buffer, buffer + toCopy);

    if (data.size() < SECTOR_SIZE)
        data.resize(SECTOR_SIZE, 0x00);

    const bool ok = cbmImage->writeSector(track, sector, data);

    if (!ok)
    {
        lastError = DriveError::BAD_SECTOR;
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

void D1571::atnChanged(bool atnLow)
{
    if (atnLow == atnLineLow) return; // ignore no change

    bool prev = atnLineLow;
    atnLineLow = atnLow;

    // Keep VIA in sync with the new ATN level (PB4 input)
    auto& via1 = d1571mem.getVIA1();
    via1.setIECInputLines(atnLineLow, clkLineLow, dataLineLow);

    // If ATN just asserted (bus high->low), this is the start of a new command
    // phase. Resync the serial shift register so we don't carry partial bytes.
    if (!prev && atnLineLow)
        via1.resetShift();

    // CA1 polarity fix: treat ATN assert (high->low on bus) as CA1 rising
    bool ca1Rising  = (!prev && atnLineLow);   // ATN high->low
    bool ca1Falling = ( prev && !atnLineLow);  // ATN low->high
    via1.onCA1Edge(ca1Rising, ca1Falling);
}

void D1571::clkChanged(bool clkLow)
{
    if (clkLow == clkLineLow)
        return;

    clkLineLow = clkLow;

    auto& via1 = d1571mem.getVIA1();
    via1.setIECInputLines(atnLineLow, clkLineLow, dataLineLow);
}

void D1571::dataChanged(bool dataLow)
{
    if (dataLow == dataLineLow) return;

    dataLineLow = dataLow;

    auto& via1 = d1571mem.getVIA1();
    via1.setIECInputLines(atnLineLow, clkLineLow, dataLineLow);
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

    peripheralAssertData(false);
    peripheralAssertClk(false);
    peripheralAssertSrq(false);
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

    peripheralAssertClk(false);
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

    peripheralAssertData(false);
    peripheralAssertClk(false);
    peripheralAssertSrq(false);
}

void D1571::onSecondaryAddress(uint8_t sa)
{
    currentSecondaryAddress = sa;

    // We’ve now consumed the secondary address; next bytes are data/commands
    expectingSecAddr  = false;
    expectingDataByte = true;
}

void D1571::onVIA2PortAWrite(uint8_t value, uint8_t ddrA)
{
    if (!diskWriteGate)
        return;

    if (!diskLoaded || !diskImage || !motorOn || diskWriteProtected)
        return;

    if (ddrA != 0xFF)
        return;

    if (!pendingWritePosValid || gcrTrack.empty())
        return;

    const size_t pos = pendingWritePos % gcrTrack.size();

    gcrTrack.getTrackData()[pos] = value;

    if (gcrWrittenMask.size() == gcrTrack.size())
        gcrWrittenMask[pos] = 1;

    trackModifiedByWrite = true;

    // Sector-based images keep the generated raw track cache updated.
    // G64 remains a live raw track and is committed when write gate closes.
    if (!getG64Image())
        saveCurrentRawTrackToCache();
}

void D1571::setDiskWriteGate(bool enabled)
{
    if (diskWriteGate == enabled)
        return;

    diskWriteGate = enabled;

    if (!enabled)
    {
        rebuildSyncMapForCurrentTrack();
        saveCurrentRawTrackToCache();

        writeGcrBuffer.clear();

        writeSyncRun = 0;
        writeAfterSync = false;
        writeGapRun = 0;

        pendingWritePos = 0;
        pendingWritePosValid = false;
    }
}

Drive::IECSnapshot D1571::snapshotIEC() const
{
    Drive::IECSnapshot s{};

    s.atnLow  = getAtnLineLow();
    s.clkLow  = getClkLineLow();
    s.dataLow = getDataLineLow();
    s.srqLow  = getSRQAsserted();

    s.drvAssertAtn  = assertAtn;
    s.drvAssertClk  = assertClk;
    s.drvAssertData = assertData;
    s.drvAssertSrq  = assertSrq;

    // Protocol state
    s.busState  = currentDriveBusState;
    s.listening = listening;
    s.talking   = talking;

    s.secondaryAddress = this->currentSecondaryAddress;

    // Legacy shifter (from Peripheral)
    s.shiftReg = shiftReg;
    s.bitsProcessed = bitsProcessed;

    // Handshake + talk queue (from Drive)
    s.waitingForAck = waitingForAck;
    s.ackEdgeCountdown = ackEdgeCountdown;
    s.swallowPostHandshakeFalling = swallowPostHandshakeFalling;
    s.waitingForClkRelease = waitingForClkRelease;
    s.prevClkLevel = prevClkLevel;
    s.ackHold = ackHold;
    s.byteAckHold = byteAckHold;
    s.ackDelay = ackDelay;
    s.talkQueueLen = talkQueue.size();

    return s;
}

void D1571::getDriveIndicators(std::vector<Indicator>& out) const
{
    out.clear();

    Indicator pwr;
    pwr.name = "PWR";
    pwr.on = isDiskLoaded();
    pwr.color = IDriveIndicatorView::DriveIndicatorColor::Red;
    out.push_back(std::move(pwr));

    Indicator act;
    act.name = "ACT";
    act.on = d1571mem.getVIA2().isLedOn();
    act.color = IDriveIndicatorView::DriveIndicatorColor::Green;
    out.push_back(std::move(act));
}

void D1571::rebuildSyncMapForCurrentTrack()
{
    gcrTrack.rebuildSyncMap();
}

size_t D1571::findHeaderPosForSector(uint8_t track, uint8_t sector) const
{
    if (gcrTrack.empty())
        return SIZE_MAX;

    constexpr size_t HEADER_GCR_SIZE = 10;

    for (size_t pos = 1; pos + HEADER_GCR_SIZE <= gcrTrack.size(); ++pos)
    {
        const size_t prev = pos - 1;

        if (prev >= gcrTrack.getSyncMap().size() || gcrTrack.getSyncMap()[prev] == 0)
            continue;

        std::vector<uint8_t> raw;
        raw.reserve(8);

        if (!gcrCodec.decodeBytes(&gcrTrack.getTrackData()[pos], HEADER_GCR_SIZE, raw))
            continue;

        if (raw.size() != 8 || raw[0] != 0x08)
            continue;

        const uint8_t decodedSector = raw[2];
        const uint8_t decodedTrack  = raw[3];
        const uint8_t id2           = raw[4];
        const uint8_t id1           = raw[5];

        const uint8_t expectedChecksum =
            static_cast<uint8_t>(decodedSector ^ decodedTrack ^ id2 ^ id1);

        if (raw[1] != expectedChecksum)
            continue;

        if (decodedTrack == track && decodedSector == sector)
            return pos;
    }

    return SIZE_MAX;
}

bool D1571::decodeRawSectorFromCurrentTrack(uint8_t track, uint8_t sector, std::vector<uint8_t>& outSector)
{
    outSector.clear();

    if (gcrTrack.empty())
        return false;

    const size_t headerPos = findHeaderPosForSector(track, sector);
    if (headerPos == SIZE_MAX)
        return false;

    const size_t n = gcrTrack.size();

    constexpr size_t DATA_GCR_SIZE = 325;

    // After the header block, search for a valid data block.
    // Do not trust one fixed offset; written sectors may shift a few bytes.
    const size_t scanStart = (headerPos + 10) % n;

    for (size_t offset = 0; offset < 128; ++offset)
    {
        const size_t dataStart = (scanStart + offset) % n;

        std::vector<uint8_t> gcrBlock;
        gcrBlock.reserve(DATA_GCR_SIZE);

        for (size_t i = 0; i < DATA_GCR_SIZE; ++i)
            gcrBlock.push_back(gcrTrack.getTrackData()[(dataStart + i) % n]);

        std::vector<uint8_t> raw;
        raw.reserve(260);

        if (!gcrCodec.decodeBytes(gcrBlock.data(), DATA_GCR_SIZE, raw))
            continue;

        if (raw.size() != 260)
            continue;

        if (raw[0] != 0x07)
            continue;

        uint8_t checksum = 0;
        for (int i = 0; i < 256; ++i)
            checksum ^= raw[1 + i];

        if (checksum != raw[257])
            continue;

        outSector.assign(raw.begin() + 1, raw.begin() + 257);
        return true;
    }

    return false;
}

void D1571::flushCurrentRawTrackToImage()
{
    if (!diskLoaded || !diskImage)
        return;

    //
    // G64 stores raw track data directly.
    //
    if (getG64Image())
    {
        saveCurrentRawTrackToCache();
        return;
    }

    //
    // D64 / D71 require a sector-addressable image.
    //
    CBMImage* cbmImage = getCBMImage();

    if (!cbmImage)
        return;

    const size_t cacheTrack = currentRawCacheIndex();

    if (cacheTrack >= rawGcrTrackDirty.size())
        return;

    if (!rawGcrTrackDirty[cacheTrack])
        return;

    // Make sure current live raw track is cached first.
    saveCurrentRawTrackToCache();

    const uint8_t trackOnSide1based = currentTrackOnSide1Based();
    const uint8_t imageTrack1based = currentImageTrack1Based();
    const int spt = gcrCodec.sectorsPerTrack1541(trackOnSide1based);

    int written = 0;
    int failed = 0;

    for (int sector = 0; sector < spt; ++sector)
    {
        std::vector<uint8_t> sectorBytes;

        if (!decodeRawSectorFromCurrentTrack(imageTrack1based, static_cast<uint8_t>(sector), sectorBytes))
        {
            ++failed;
            continue;
        }

        if (cbmImage->writeSector(imageTrack1based, static_cast<uint8_t>(sector), sectorBytes))
            ++written;
        else
            ++failed;
    }

    rawGcrTrackDirty[cacheTrack] = false;
}

void D1571::flushAllDirtyRawTracksToImage()
{
    // G64 raw tracks never get converted back into CBM sectors.
    if (getG64Image())
    {
        saveCurrentRawTrackToCache();
        return;
    }

    if (!diskLoaded || !diskImage)
        return;

    saveCurrentRawTrackToCache();

    const uint8_t oldTrack = currentTrack;
    const bool oldSide = currentSide;
    const size_t oldPos = gcrPos;

    for (size_t t = 0; t < rawGcrTrackDirty.size(); ++t)
    {
        if (!rawGcrTrackDirty[t])
            continue;

        if (!rawGcrTrackValid[t])
            continue;

        if (mediaPath == MediaPath::GCR_D71 && t >= 35)
        {
            currentSide = true;
            currentTrack = static_cast<uint8_t>(t - 35);
        }
        else
        {
            currentSide = false;
            currentTrack = static_cast<uint8_t>(t);
        }

        gcrTrack.getTrackData() = rawGcrTrackCache[t];
        gcrTrack.getSyncMap()        = rawGcrSyncCache[t];
        gcrSectorAtPos = rawGcrSectorCache[t];

        if (!gcrTrack.empty())
            gcrPos %= gcrTrack.size();
        else
            gcrPos = 0;

        flushCurrentRawTrackToImage();
    }

    currentTrack = oldTrack;
    currentSide = oldSide;
    gcrPos = oldPos;

    const size_t restoreIndex = currentRawCacheIndex();

    if (restoreIndex < rawGcrTrackValid.size() && rawGcrTrackValid[restoreIndex])
    {
        gcrTrack.getTrackData() = rawGcrTrackCache[restoreIndex];
        gcrTrack.getSyncMap()        = rawGcrSyncCache[restoreIndex];
        gcrSectorAtPos = rawGcrSectorCache[restoreIndex];
    }
    else
    {
        gcrDirty = true;
    }
}

void D1571::invalidateRawGcrCache()
{
    for (auto& t : rawGcrTrackCache)
        t.clear();

    for (auto& s : rawGcrSyncCache)
        s.clear();

    for (auto& p : rawGcrSectorCache)
        p.clear();

    rawGcrTrackValid.fill(false);
    rawGcrTrackDirty.fill(false);

    gcrTrack.clear();
    gcrTrack.getSyncMap().clear();
    gcrSectorAtPos.clear();
    gcrWrittenMask.clear();

    gcrPos = 0;
    gcrDirty = true;
}

void D1571::flushAndSaveDisk()
{
    if (!diskImage || loadedDiskName.empty())
        return;

    if (getG64Image())
    {
        saveCurrentRawTrackToCache();

        if (diskImage->isDirty())
        {
            if (diskImage->saveDisk(loadedDiskName))
                diskImage->clearDirty();
        }

        return;
    }

    flushAllDirtyRawTracksToImage();

    if (diskImage->isDirty())
    {
        if (diskImage->saveDisk(loadedDiskName))
            diskImage->clearDirty();
    }
}

void D1571::saveCurrentRawTrackToCache()
{
    if (G64* g64Image = getG64Image())
    {
        if (gcrTrack.empty())
            return;

        if (!trackModifiedByWrite)
            return;

        const size_t trackIndex = static_cast<size_t>(halfTrackPos);

        if (g64Image->setTrackData(trackIndex, gcrTrack.getTrackData()))
           trackModifiedByWrite = false;

        return;
    }

    const size_t t = currentRawCacheIndex();

    if (t >= rawGcrTrackCache.size())
        return;

    if (!trackModifiedByWrite)
        return;

    if (gcrTrack.empty())
        return;

    rawGcrTrackCache[t]  = gcrTrack.getTrackData();
    rawGcrSyncCache[t]   = gcrTrack.getSyncMap();
    rawGcrSectorCache[t] = gcrSectorAtPos;

    rawGcrTrackValid[t] = true;
    rawGcrTrackDirty[t] = true;

    trackModifiedByWrite = false;
}

void D1571::loadCurrentRawTrackFromCacheOrBuild()
{
    //
    // G64 raw-track path
    //
    if (G64* g64Image = getG64Image())
    {
        gcrTrack.clear();
        gcrTrack.getSyncMap().clear();
        gcrSectorAtPos.clear();
        gcrWrittenMask.clear();

        const size_t g64TrackIndex = static_cast<size_t>(halfTrackPos);

        if (!g64Image->hasTrack(g64TrackIndex))
        {
            gcrPos = 0;
            d1571mem.getVIA2().clearMechBytePending();
            return;
        }

        gcrTrack.setTrackData(g64Image->getTrackData(g64TrackIndex));
        gcrTrack.setSpeedZones(g64Image->getTrackSpeedZones(g64TrackIndex));

        if (gcrTrack.empty())
        {
            gcrPos = 0;
            d1571mem.getVIA2().clearMechBytePending();
            return;
        }

        rebuildSyncMapForCurrentTrack();

        // G64 has no prebuilt sector-position map.
        // We'll decode sector headers while the disk rotates later.
        gcrSectorAtPos.assign(gcrTrack.size(), currentSector);
        gcrWrittenMask.assign(gcrTrack.size(), 0);

        gcrPos %= gcrTrack.size();

        d1571mem.getVIA2().clearMechBytePending();

        return;
    }

    //
    // Existing D64 / D71 cache path
    //
    const size_t cacheTrack = currentRawCacheIndex();

    if (cacheTrack < rawGcrTrackValid.size() && rawGcrTrackValid[cacheTrack])
    {
        gcrTrack.getTrackData() = rawGcrTrackCache[cacheTrack];
        gcrTrack.getSyncMap() = rawGcrSyncCache[cacheTrack];
        gcrSectorAtPos = rawGcrSectorCache[cacheTrack];

        if (gcrWrittenMask.size() != gcrTrack.size())
            gcrWrittenMask.assign(gcrTrack.size(), 0);

        if (!gcrTrack.empty())
            gcrPos %= gcrTrack.size();
        else
            gcrPos = 0;

        d1571mem.getVIA2().clearMechBytePending();
        return;
    }

    rebuildGCRTrackStream();

    if (cacheTrack < rawGcrTrackCache.size())
    {
        rawGcrTrackCache[cacheTrack] = gcrTrack.getTrackData();
        rawGcrSyncCache[cacheTrack] = gcrTrack.getSyncMap();
        rawGcrSectorCache[cacheTrack] = gcrSectorAtPos;
        rawGcrTrackValid[cacheTrack] = true;
        rawGcrTrackDirty[cacheTrack] = false;
    }
}

void D1571::sampleHeaderAtCurrentPosition(size_t pos)
{
    if (gcrTrack.empty())
        return;

    constexpr size_t HEADER_GCR_SIZE = 10;

    const size_t n = gcrTrack.size();

    if (pos >= n)
        return;

    const size_t prev = (pos + n - 1) % n;

    if (prev >= gcrTrack.getSyncMap().size() || gcrTrack.getSyncMap()[prev] == 0)
        return;

    uint8_t gcrHeader[HEADER_GCR_SIZE];

    for (size_t i = 0; i < HEADER_GCR_SIZE; ++i)
        gcrHeader[i] = gcrTrack.getTrackData()[(pos + i) % n];

    std::vector<uint8_t> raw;
    raw.reserve(8);

    if (!gcrCodec.decodeBytes(gcrHeader, HEADER_GCR_SIZE, raw))
        return;

    if (raw.size() != 8 || raw[0] != 0x08)
        return;

    const uint8_t sector = raw[2];
    const uint8_t track  = raw[3];
    const uint8_t id2    = raw[4];
    const uint8_t id1    = raw[5];

    const uint8_t expectedChecksum = static_cast<uint8_t>(sector ^ track ^ id2 ^ id1);

    if (raw[1] != expectedChecksum)
        return;

    if (track < 1 || track > 35)
        return;

    if (sector >= gcrCodec.sectorsPerTrack1541(track))
        return;

    if (getG64Image())
        currentSector = sector;
}

CBMImage* D1571::getCBMImage()
{
    return dynamic_cast<CBMImage*>(diskImage.get());
}

const CBMImage* D1571::getCBMImage() const
{
    return dynamic_cast<const CBMImage*>(diskImage.get());
}

G64* D1571::getG64Image()
{
    return dynamic_cast<G64*>(diskImage.get());
}

const G64* D1571::getG64Image() const
{
    return dynamic_cast<const G64*>(diskImage.get());
}

uint8_t D1571::currentTrackOnSide1Based() const
{
    return static_cast<uint8_t>(currentTrack + 1);
}

uint8_t D1571::currentImageTrack1Based() const
{
    const uint8_t trackOnSide = currentTrackOnSide1Based();

    if (mediaPath == MediaPath::GCR_D71 && currentSide)
        return static_cast<uint8_t>(trackOnSide + 35);

    return trackOnSide;
}

size_t D1571::currentRawCacheIndex() const
{
    if (mediaPath == MediaPath::GCR_D71 && currentSide)
        return static_cast<size_t>(currentTrack + 35);

    return static_cast<size_t>(currentTrack);
}

void D1571::resetForMediaChange()
{
    // --- D1571-level IEC flags ---
    atnLineLow                  = false;
    clkLineLow                  = false;
    dataLineLow                 = false;
    srqAsserted                 = false;

    iecListening                = false;
    iecTalking                  = false;

    presenceAckDone             = false;
    expectingSecAddr            = false;
    expectingDataByte           = false;

    currentListenSA             = 0;
    currentTalkSA               = 0;

    iecRxActive                 = false;
    iecRxBitCount               = 0;
    iecRxByte                   = 0;

    // --- Drive/Peripheral protocol abort ---
    listening                   = false;
    talking                     = false;

    currentSecondaryAddress     = 0xFF;

    shiftReg                    = 0;
    bitsProcessed               = 0;

    waitingForAck               = false;
    ackEdgeCountdown            = 0;
    swallowPostHandshakeFalling = false;
    waitingForClkRelease        = false;
    prevClkLevel                = true;
    ackHold                     = false;
    byteAckHold                 = false;
    ackDelay                    = 0;

    status                      = DriveStatus::IDLE;

    while (!talkQueue.empty())
        talkQueue.pop();

    currentDriveBusState = DriveBusState::IDLE;

    // --- 1571 transient clears ---
    d1571mem.getVIA2().clearMechBytePending();

    // --- Release actual IEC line outputs ---
    peripheralAssertClk(false);
    peripheralAssertData(false);
    peripheralAssertSrq(false);

    // --- Drop bus associations ---
    if (iecBus)
    {
        iecBus->unTalk(deviceNumber);
        iecBus->unListen(deviceNumber);
    }

    // --- Media/GCR reset ---
    gcrPos = 0;
    gcrBitCounter = 0;
    gcrDirty = true;

    diskWriteGate = false;
    pendingWritePos = 0;
    pendingWritePosValid = false;
    trackModifiedByWrite = false;

    writeSyncRun = 0;
    writeAfterSync = false;
    writeGapRun = 0;

    gcrTrack.clear();
    gcrTrack.getSyncMap().clear();
    gcrSectorAtPos.clear();
    gcrWrittenMask.clear();
    writeGcrBuffer.clear();

    invalidateRawGcrCache();

    // 1571-specific runtime cleanup.
    busDriversEnabled = false;
    twoMHzMode = false;

    uint16_t pc = driveCPU.getPC();

    if (!(pc < 0x0800))
        driveCPU.reset();

    forceSyncIEC();
    updateIRQ();
}
