// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Drive/D1541.h"
#include "IECBUS.h"

D1541::D1541(int deviceNumber, const std::string& loRom, const std::string& hiRom) :
    motorOn(false),
    diskLoaded(false),
    diskWriteProtected(false),
    atnLineLow(false),
    clkLineLow(false),
    dataLineLow(false),
    srqAsserted(false),
    iecLinesPrimed(false),
    iecListening(false),
    iecTalking(false),
    presenceAckDone(false),
    expectingSecAddr(false),
    expectingDataByte(false),
    currentListenSA(0),
    currentTalkSA(0),
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
    d1541mem.attachPeripheralInstance(this);

    d1541Bus.attachMemoryInstance(&d1541mem);

    driveCPU.attachCPUBusInstance(&d1541Bus);
    driveCPU.attachIRQLineInstance(&IRQ);

    if (!d1541mem.initialize(loRom, hiRom))
    {
        throw std::runtime_error("Unable to start drive, ROM not loaded!\n");
    }

    reset();
}

D1541::~D1541() = default;

void D1541::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("D541");

    wrtr.writeU32(2);
    wrtr.writeU8(static_cast<uint8_t>(deviceNumber));

    wrtr.writeBool(diskLoaded);
    wrtr.writeBool(diskWriteProtected);
    wrtr.writeString(loadedDiskName);

    // CPU state
    driveCPU.saveStatePayload(wrtr);
    driveCPU.saveStateExtendedPayload(wrtr);

    // Mechanics / GCR
    wrtr.writeBool(motorOn);
    wrtr.writeU8(currentTrack);
    wrtr.writeU8(currentSector);
    wrtr.writeU8(densityCode);
    wrtr.writeI32(halfTrackPos);
    wrtr.writeI32(gcrBitCounter);
    wrtr.writeU32(static_cast<uint32_t>(gcrPos));

    // IEC protocol state
    wrtr.writeBool(atnLineLow);
    wrtr.writeBool(clkLineLow);
    wrtr.writeBool(dataLineLow);
    wrtr.writeBool(srqAsserted);
    wrtr.writeBool(iecLinesPrimed);
    wrtr.writeBool(iecListening);
    wrtr.writeBool(iecTalking);
    wrtr.writeBool(presenceAckDone);
    wrtr.writeBool(expectingSecAddr);
    wrtr.writeBool(expectingDataByte);
    wrtr.writeU8(currentListenSA);
    wrtr.writeU8(currentTalkSA);
    wrtr.writeBool(iecRxActive);
    wrtr.writeI32(iecRxBitCount);
    wrtr.writeU8(iecRxByte);

    // UI state
    wrtr.writeU8(uiTrack);
    wrtr.writeU8(uiSector);

    // RAM and chips
    d1541mem.saveState(wrtr);
    d1541mem.getVIA1().saveState(wrtr);
    d1541mem.getVIA2().saveState(wrtr);

    wrtr.endChunk();
}

bool D1541::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "D541", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    uint32_t ver = 0;

    if (!rdr.readU32(ver))                                  { rdr.exitChunkPayload(chunk); return false; }
    if (ver != 2)                                           { rdr.exitChunkPayload(chunk); return false; }

    uint8_t dev = 0;
    if (!rdr.readU8(dev))                                   { rdr.exitChunkPayload(chunk); return false; }

    setDeviceNumber(static_cast<int>(dev));

    bool savedDiskLoaded = false;
    bool savedWriteProtected = false;
    std::string savedDiskName;

    if (!rdr.readBool(savedDiskLoaded))                     { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(savedWriteProtected))                 { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readString(savedDiskName))                     { rdr.exitChunkPayload(chunk); return false; }

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

    diskLoaded = savedDiskLoaded;
    diskWriteProtected = savedWriteProtected;
    loadedDiskName = savedDiskLoaded ? savedDiskName : std::string{};

    if (!driveCPU.loadStatePayload(rdr))                    { rdr.exitChunkPayload(chunk); return false; }
    if (!driveCPU.loadStateExtendedPayload(chunk, rdr))     { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readBool(motorOn))                             { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readU8(currentTrack))                          { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readU8(currentSector))                         { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readU8(densityCode))                           { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readI32(halfTrackPos))                         { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readI32(gcrBitCounter))                        { rdr.exitChunkPayload(chunk); return false; }

    uint32_t savedGcrPos = 0;

    if (!rdr.readU32(savedGcrPos))                          { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readBool(atnLineLow))                          { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(clkLineLow))                          { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(dataLineLow))                         { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(srqAsserted))                         { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(iecLinesPrimed))                      { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(iecListening))                        { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(iecTalking))                          { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(presenceAckDone))                     { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(expectingSecAddr))                    { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(expectingDataByte))                   { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readU8(currentListenSA))                       { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readU8(currentTalkSA))                         { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(iecRxActive))                         { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readI32(iecRxBitCount))                        { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readU8(iecRxByte))                             { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readU8(uiTrack))                               { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readU8(uiSector))                              { rdr.exitChunkPayload(chunk); return false; }

    if (!d1541mem.loadState(rdr))                           { rdr.exitChunkPayload(chunk); return false; }
    if (!d1541mem.getVIA1().loadState(rdr))                 { rdr.exitChunkPayload(chunk); return false; }
    if (!d1541mem.getVIA2().loadState(rdr))                 { rdr.exitChunkPayload(chunk); return false; }

    rdr.exitChunkPayload(chunk);

    // The raw GCR vectors and caches are not serialized.
    // Rebuild them lazily while retaining the rotational position.
    invalidateRawGcrCache();

    gcrPos = static_cast<size_t>(savedGcrPos);
    gcrDirty = true;

    // Reapply live signal outputs and derived IRQ state.
    forceSyncIEC();
    peripheralAssertSrq(srqAsserted);
    updateIRQ();

    uiLedWasOn = d1541mem.getVIA2().isLedOn();

    return true;
}

void D1541::reset()
{
    // Mechanics
    motorOn = false;

    // Status
    lastError                   = DriveError::NONE;
    status                      = DriveStatus::IDLE;

    // Disk
    diskLoaded                  = false;
    diskWriteProtected          = false;
    currentTrack                = 17;
    currentSector               = 0;
    densityCode                 = 3;
    halfTrackPos                = currentTrack * 2;
    loadedDiskName.clear();

    // IEC BUS reset
    atnLineLow                  = false;
    clkLineLow                  = false;
    dataLineLow                 = false;
    srqAsserted                 = false;
    iecLinesPrimed              = false;
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

    // Reset actual line states
    peripheralAssertClk(false);  // Release Clock
    peripheralAssertData(false); // Release Data
    peripheralAssertSrq(false);  // Release SRQ

    if (iecBus)
    {
        iecBus->unTalk(deviceNumber);
        iecBus->unListen(deviceNumber);
    }

    gcrPos                      = 0;
    gcrBitCounter               = 0;
    gcrDirty                    = true;
    lastHeaderTrack             = 0;
    lastHeaderSector            = 0;
    haveLastHeader              = false;
    diskWriteGate               = false;
    pendingWritePos             = 0;
    pendingWritePosValid        = false;
    trackModifiedByWrite        = false;
    lastHeaderPos               = 0;
    lastHeaderValid             = false;
    readSyncRun                 = 0;
    readAfterSync               = false;
    lastRomHeaderTrack          = 0;
    lastRomHeaderSector         = 0;
    lastRomHeaderValid          = false;
    lastRomHeaderPos            = 0;
    lastRomHeaderCycle          = 0;
    writeSyncRun                = 0;
    writeAfterSync              = false;
    writeGapRun                 = 0;

    readGcrHeaderProbe.clear();
    writeGcrBuffer.clear();
    gcrTrack.clear();
    gcrSectorAtPos.clear();
    gcrWrittenMask.clear();
    invalidateRawGcrCache();

    d1541mem.reset();
    driveCPU.reset();

    // UI activity
    uiTrack                     = currentTrack;
    uiSector                    = currentSector;
    uiLedWasOn                  = false;

    forceSyncIEC();
    updateIRQ();
}

void D1541::tick(uint32_t cycles)
{
    int32_t remaining = cycles;

    while (remaining > 0)
    {
        if (checkBreakpoint())
            return;

        driveCPU.tick();
        uint32_t dc = driveCPU.getElapsedCycles();
        if (dc == 0) dc = 1;

        d1541mem.tick(dc);

        if (motorOn && diskLoaded)
            gcrAdvance(dc);

        const bool ledOn = d1541mem.getVIA2().isLedOn();

        if (ledOn)
            uiTrack = currentTrack;

        if (ledOn && !uiLedWasOn)
        {
            uiSector = currentSector;
        }

        uiLedWasOn = ledOn;

        remaining -= dc;
    }
}

bool D1541::gcrTick()
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

    if (gcrTrack.empty())
        return false;

    if (gcrTrack.getSyncMap().size() != gcrTrack.size())
        gcrTrack.getSyncMap().assign(gcrTrack.size(), 0);

    if (gcrSectorAtPos.size() != gcrTrack.size())
        gcrSectorAtPos.assign(gcrTrack.size(), currentSector);

    const size_t pos = gcrPos;

    const uint8_t gcrByte = gcrTrack.getTrackData()[pos];
    const bool syncHigh   = (gcrTrack.getSyncMap()[pos] != 0);
    const uint8_t sectorNow = gcrSectorAtPos[pos];

    // Sector tags exist for generated CBMImage tracks.
    // Raw G64 tracks determine the current sector from decoded headers.
    if (!getG64Image())
        currentSector = sectorNow;

    if (!diskWriteGate)
        sampleHeaderAtCurrentPosition(pos);

    gcrPos = (gcrPos + 1) % gcrTrack.size();

    if (diskWriteGate && motorOn && diskLoaded && diskImage && !diskWriteProtected)
    {
        pendingWritePos = pos;
        pendingWritePosValid = true;
        d1541mem.getVIA2().pulseWriteByteReady();
        return true;
    }

    d1541mem.getVIA2().diskByteFromMedia(gcrByte, syncHigh);

    return true;
}

void D1541::gcrAdvance(uint32_t dc)
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

        const int cyclesPerByte = cyclesPerByteFromDensity(activeDensity);

        if (gcrBitCounter < cyclesPerByte)
            break;

        gcrBitCounter -= cyclesPerByte;

        gcrTick();
    }
}

void D1541::rebuildGCRTrackStream()
{
    gcrTrack.clear();
    gcrTrack.getSyncMap().clear();
    gcrSectorAtPos.clear();

    if (!diskLoaded || !diskImage)
        return;

    CBMImage* cbmImage = getCBMImage();

    // This function builds a GCR track from sector-addressable media.
    // G64 will have its own raw-track path.
    if (!cbmImage)
        return;

    const int track1based = int(currentTrack) + 1;
    const int spt = gcrCodec.sectorsPerTrack1541(track1based);

    auto bam = cbmImage->readSector(18, 0);
    if (bam.size() < 256)
        bam.resize(256, 0x00);

    const uint8_t id1 = bam[0xA2];
    const uint8_t id2 = bam[0xA3];

    auto pushN = [&](uint8_t v, int count, bool isSync, uint8_t sectorTag)
    {
        auto& trackData = gcrTrack.getTrackData();
        trackData.insert(trackData.end(), count, v);
        gcrTrack.getSyncMap().insert(gcrTrack.getSyncMap().end(), count, isSync ? 1 : 0);
        gcrSectorAtPos.insert(gcrSectorAtPos.end(), count, sectorTag);
    };

    auto pushEncoded = [&](const uint8_t* in, size_t len, uint8_t sectorTag)
    {
        for (size_t i = 0; i < len; i += 4)
        {
            uint8_t g[5];

            gcrCodec.encode4Bytes(&in[i], g);

            auto& trackData = gcrTrack.getTrackData();
            trackData.insert(trackData.end(), g, g + 5);
            gcrTrack.getSyncMap().insert(gcrTrack.getSyncMap().end(), 5, 0);
            gcrSectorAtPos.insert(gcrSectorAtPos.end(), 5, sectorTag);
        }
    };

    // Same "DOS-ish defaults" as D1571
    constexpr int SYNC_LEN   = 10;
    constexpr int HEADER_GAP = 9;
    constexpr int TAIL_GAP   = 9;

    // Lead-in gap (NOT sync)
    pushN(0x55, 64, false, 0);

    for (int sector = 0; sector < spt; ++sector)
    {
        std::vector<uint8_t> sec = cbmImage->readSector(static_cast<uint8_t>(track1based), static_cast<uint8_t>(sector));

        if (sec.size() != 256)
            sec.assign(256, 0x00);

        // ---- HEADER ----
        pushN(0xFF, SYNC_LEN, true, static_cast<uint8_t>(sector));

        uint8_t hdr[8] = {0};

        hdr[0] = 0x08;
        hdr[2] = static_cast<uint8_t>(sector);
        hdr[3] = static_cast<uint8_t>(track1based);
        hdr[4] = id2;
        hdr[5] = id1;
        hdr[6] = 0x0F;
        hdr[7] = 0x0F;

        hdr[1] =
            static_cast<uint8_t>(
                hdr[2] ^
                hdr[3] ^
                hdr[4] ^
                hdr[5]);

        pushEncoded(hdr, 8, static_cast<uint8_t>(sector));
        pushN(0x55, HEADER_GAP, false, static_cast<uint8_t>(sector));

        // ---- DATA ----
        pushN(0xFF, SYNC_LEN, true, static_cast<uint8_t>(sector));

        std::vector<uint8_t> raw(260, 0x00);

        raw[0] = 0x07;

        uint8_t csum = 0;

        for (int i = 0; i < 256; ++i)
        {
            raw[1 + i] = sec[i];
            csum ^= raw[1 + i];
        }

        raw[257] = csum;
        raw[258] = 0x00;
        raw[259] = 0x00;

        pushEncoded(raw.data(), raw.size(), static_cast<uint8_t>(sector));
        pushN(0x55, TAIL_GAP, false, static_cast<uint8_t>(sector));
    }

    // Trailing gap
    pushN(0x55, 128, false, 0);

    // Sanity
    if (gcrTrack.getSyncMap().size() != gcrTrack.size())
        gcrTrack.getSyncMap().assign(gcrTrack.size(), 0);

    gcrPos = 0;

    gcrWrittenMask.assign(gcrTrack.size(), 0);

    d1541mem.getVIA2().clearMechBytePending();
}

void D1541::updateIRQ()
{
    bool via1IRQ = d1541mem.getVIA1().checkIRQActive();
    bool via2IRQ = d1541mem.getVIA2().checkIRQActive();

    bool any = via1IRQ || via2IRQ;

    if (any) IRQ.raiseIRQ(IRQLine::D1541_IRQ);
    else IRQ.clearIRQ(IRQLine::D1541_IRQ);
}

void D1541::loadDisk(const std::string& path)
{
    resetForMediaChange();

    auto img = DiskFactory::create(path);

    if (!img)
    {
        // "Door open / no media" behavior: don't reset the drive computer
        loadedDiskName.clear();
        diskImage.reset();
        diskLoaded = false;
        lastError = DriveError::NO_DISK;

        // Invalidate any ongoing media stream
        gcrDirty = true;
        gcrPos = 0;
        gcrBitCounter = 0;

        gcrTrack.clear();
        gcrTrack.getSyncMap().clear();
        gcrSectorAtPos.clear();
        d1541mem.getVIA2().clearMechBytePending();
        return;
    }

    if (!img->loadDisk(path))
    {
        // "Door open / no media" behavior: don't reset the drive computer
        loadedDiskName.clear();
        diskImage.reset();
        diskLoaded = false;
        lastError = DriveError::NO_DISK;

        // Invalidate any ongoing media stream
        gcrDirty = true;
        gcrPos = 0;
        gcrBitCounter = 0;

        gcrTrack.clear();
        gcrTrack.getSyncMap().clear();
        gcrSectorAtPos.clear();
        d1541mem.getVIA2().clearMechBytePending();
        return;
    }

    // HOT SWAP
    diskImage = std::move(img);

    diskWriteProtected = diskImage->isWriteProtected();

    diskLoaded = true;

    invalidateRawGcrCache();

    loadedDiskName = path;
    status = DriveStatus::READY;
    lastError = DriveError::NONE;

    // Invalidate/rebuild media stream for the newly inserted disk
    gcrDirty = true;
    gcrPos = 0;
    gcrBitCounter = 0;

    gcrTrack.clear();
    gcrTrack.getSyncMap().clear();
    gcrSectorAtPos.clear();
    d1541mem.getVIA2().clearMechBytePending();
}

void D1541::unloadDisk()
{
    flushAndSaveDisk();

    diskImage.reset();  // Reset disk image by assigning a fresh instance
    loadedDiskName.clear();

    gcrPos = 0;
    gcrBitCounter = 0;
    gcrTrack.clear();
    gcrTrack.getSyncMap().clear();
    gcrSectorAtPos.clear();
    writeGcrBuffer.clear();
    gcrWrittenMask.clear();

    diskWriteGate           = false;
    pendingWritePos         = 0;
    pendingWritePosValid    = false;
    trackModifiedByWrite    = false;
    invalidateRawGcrCache();

    diskLoaded              = false;
    currentTrack            = 17;
    currentSector           = 0;
    uiTrack                 = currentTrack;
    uiSector                = currentSector;
    uiLedWasOn              = false;
    lastError               = DriveError::NONE;
    status                  = DriveStatus::IDLE;
    lastHeaderTrack         = 0;
    lastHeaderSector        = 0;
    haveLastHeader          = false;

}

void D1541::onListen()
{
    // IEC bus has selected this drive as a listener
    iecListening            = true;
    iecTalking              = false;

    listening               = true;
    talking                 = false;
    iecRxActive             = true;
    iecRxBitCount           = 0;
    iecRxByte               = 0;

    // We're about to receive a secondary address byte after LISTEN
    presenceAckDone         = false;   // so we do the LISTEN presence ACK
    expectingSecAddr        = true;    // first byte after LISTEN is secondary address
    expectingDataByte       = false;
    currentSecondaryAddress = 0xFF;  // "none" / invalid

    status                  = DriveStatus::READY;

    #ifdef Debug
    std::cout << "[D1541] onListen() device=" << int(deviceNumber)
              << " listening=1 talking=0\n";
    #endif
}

void D1541::onUnListen()
{
    iecListening        = false;
    listening           = false;
    iecRxActive         = false;
    iecRxBitCount       = 0;
    iecRxByte           = 0;

    expectingSecAddr    = false;
    expectingDataByte   = false;

    status              = DriveStatus::IDLE;

    // After TALK, the next byte from the C64 is a secondary address
    expectingSecAddr        = true;
    expectingDataByte       = false;
    currentSecondaryAddress = 0xFF;

    peripheralAssertData(false);

    #ifdef Debug
    std::cout << "[D1541] onUnListen() device=" << int(deviceNumber) << "\n";
    #endif // Debug
}

void D1541::onTalk()
{
    iecTalking              = true;
    iecListening            = false;

    talking                 = true;
    listening               = false;
    iecRxActive             = false;
    iecRxBitCount           = 0;
    iecRxByte               = 0;
    presenceAckDone         = false;

    status                  = DriveStatus::READY;

    peripheralAssertClk(false);

    #ifdef Debug
    std::cout << "[D1541] onTalk() device=" << int(deviceNumber)
              << " talking=1 listening=0\n";
    #endif
}

void D1541::onUnTalk()
{
    iecTalking          = false;
    talking             = false;
    iecRxActive         = false;
    iecRxBitCount       = 0;
    iecRxByte           = 0;

    expectingSecAddr    = false;
    expectingDataByte   = false;

    status              = DriveStatus::IDLE;

    // After TALK, the next byte from the C64 is a secondary address
    expectingSecAddr        = true;
    expectingDataByte       = false;
    currentSecondaryAddress = 0xFF;

    #ifdef Debug
    std::cout << "[D1541] onUnTalk() device=" << int(deviceNumber) << "\n";
    #endif
}

void D1541::onSecondaryAddress(uint8_t sa)
{
    currentSecondaryAddress = sa;
    expectingSecAddr  = false;
    expectingDataByte = true;

    if (sa == 0)
        status = DriveStatus::READING;
    else if (sa == 1)
        status = DriveStatus::WRITING;
    else
        status = DriveStatus::READY;
}

void D1541::atnChanged(bool atnLow)
{
    // Always forward the very first notification so VIA1 gets a baseline sample
    if (iecLinesPrimed && atnLow == atnLineLow) return;

    bool prevAtnLow = atnLineLow;
    atnLineLow = atnLow;

    auto& via1 = d1541mem.getVIA1();
    via1.setIECInputLines(atnLineLow, clkLineLow, dataLineLow);

    // Only reset fast-serial shift on a real ATN falling edge (high->low)
    if (!prevAtnLow && atnLineLow)
        via1.resetShift();

    iecLinesPrimed = true;
}

void D1541::clkChanged(bool clkLow)
{
    if (iecLinesPrimed && clkLow == clkLineLow) return;

    clkLineLow = clkLow;

    auto& via1 = d1541mem.getVIA1();
    via1.setIECInputLines(atnLineLow, clkLineLow, dataLineLow);

    iecLinesPrimed = true;
}

void D1541::dataChanged(bool dataLow)
{
    if (iecLinesPrimed && dataLow == dataLineLow) return;

    dataLineLow = dataLow;

    auto& via1 = d1541mem.getVIA1();
    via1.setIECInputLines(atnLineLow, clkLineLow, dataLineLow);

    iecLinesPrimed = true;
}

void D1541::setDensityCode(uint8_t code)
{
    code &= 0x03;
    if (densityCode != code) densityCode = code;
}

void D1541::onVIA2PortAWrite(uint8_t value, uint8_t ddrA)
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

    // D64 keeps its generated raw-track cache synchronized during writes.
    // G64 is already operating directly on the live raw track and is
    // committed back to the image when the write gate closes.
    if (!getG64Image())
        saveCurrentRawTrackToCache();

    acceptGCRWriteByte(value);
}

void D1541::acceptGCRWriteByte(uint8_t value)
{
    writeGcrBuffer.push_back(value);

    // Keep bounded so noise does not grow forever.
    if (writeGcrBuffer.size() > 4096)
        writeGcrBuffer.erase(writeGcrBuffer.begin(), writeGcrBuffer.begin() + 1024);
}

void D1541::tryDecodeWrittenGCR()
{
    CBMImage* cbmImage = getCBMImage();

    if (!cbmImage)
        return;

    constexpr size_t HEADER_GCR_SIZE = 10;   // 8 raw bytes encoded as 10 GCR bytes
    constexpr size_t DATA_GCR_SIZE   = 325;  // 260 raw bytes encoded as 325 GCR bytes

    auto decodeHeaderAt = [&](size_t pos, uint8_t& outTrack, uint8_t& outSector) -> bool
    {
        if (pos + HEADER_GCR_SIZE > writeGcrBuffer.size())
            return false;

        std::vector<uint8_t> raw;
        raw.reserve(8);

        if (!gcrCodec.decodeBytes(&writeGcrBuffer[pos], HEADER_GCR_SIZE, raw))
            return false;

        if (raw.size() != 8)
            return false;

        if (raw[0] != 0x08)
            return false;

        const uint8_t sector = raw[2];
        const uint8_t track  = raw[3];
        const uint8_t id2    = raw[4];
        const uint8_t id1    = raw[5];

        const uint8_t expectedChecksum = static_cast<uint8_t>(sector ^ track ^ id2 ^ id1);

        if (raw[1] != expectedChecksum)
            return false;

        if (track < 1 || track > 35)
            return false;

        if (sector >= gcrCodec.sectorsPerTrack1541(track))
            return false;

        outTrack  = track;
        outSector = sector;
        return true;
    };

    auto decodeDataAt = [&](size_t pos, std::vector<uint8_t>& outSectorData) -> bool
    {
        if (pos + DATA_GCR_SIZE > writeGcrBuffer.size())
            return false;

        std::vector<uint8_t> raw;
        raw.reserve(260);

        if (!gcrCodec.decodeBytes(&writeGcrBuffer[pos], DATA_GCR_SIZE, raw))
            return false;

        if (raw.size() != 260)
            return false;

        if (raw[0] != 0x07)
            return false;

        uint8_t checksum = 0;
        for (int i = 1; i <= 256; ++i)
            checksum ^= raw[i];

        if (checksum != raw[257])
            return false;

        outSectorData.assign(raw.begin() + 1, raw.begin() + 257);
        return true;
    };

    bool madeProgress = true;

    while (madeProgress)
    {
        madeProgress = false;

        for (size_t pos = 0; pos < writeGcrBuffer.size(); ++pos)
        {
            uint8_t headerTrack = 0;
            uint8_t headerSector = 0;

            if (decodeHeaderAt(pos, headerTrack, headerSector))
            {
                lastHeaderTrack  = headerTrack;
                lastHeaderSector = headerSector;
                haveLastHeader   = true;

                writeGcrBuffer.erase(writeGcrBuffer.begin(), writeGcrBuffer.begin() + pos + HEADER_GCR_SIZE);

                madeProgress = true;
                break;
            }

            std::vector<uint8_t> sectorData;

            if (haveLastHeader && decodeDataAt(pos, sectorData))
            {
                if (lastHeaderTrack >= 1 && lastHeaderTrack <= 35 && lastHeaderSector < gcrCodec.sectorsPerTrack1541(lastHeaderTrack))
                {
                    cbmImage->writeSector(lastHeaderTrack, lastHeaderSector, sectorData);

                    // Rebuild generated GCR stream from the updated sector data.
                    gcrDirty = true;
                }

                writeGcrBuffer.erase(writeGcrBuffer.begin(), writeGcrBuffer.begin() + pos + DATA_GCR_SIZE);

                madeProgress = true;
                break;
            }
        }
    }
}

void D1541::onStepperPhaseChange(uint8_t oldPhase, uint8_t newPhase)
{
    const int oldIdx = stepIndex(oldPhase);
    const int newIdx = stepIndex(newPhase);

    if (oldIdx < 0 || newIdx < 0 || oldIdx == newIdx)
        return;

    // delta in [0..7]
    const int delta = (newIdx - oldIdx + 8) & 7;

    int step = 0;
    if (delta == 2)      step = +1;   // forward one half-track
    else if (delta == 6) step = -1;   // backward one half-track
    else
        return; // ignore illegal jumps (delta 2..6)

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

int D1541::cyclesPerByteFromDensity(uint8_t code) const
{
    static constexpr int kCycles[4] = { 32, 30, 28, 26 };

    return kCycles[code & 0x03];
}


void D1541::saveCurrentRawTrackToCache()
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

    if (currentTrack >= rawGcrTrackCache.size())
        return;

    if (gcrTrack.empty())
        return;

    rawGcrTrackCache[currentTrack]  = gcrTrack.getTrackData();
    rawGcrSyncCache[currentTrack]   = gcrTrack.getSyncMap();
    rawGcrSectorCache[currentTrack] = gcrSectorAtPos;
    rawGcrTrackValid[currentTrack]  = true;

    if (trackModifiedByWrite)
        rawGcrTrackDirty[currentTrack] = true;
}

void D1541::loadCurrentRawTrackFromCacheOrBuild()
{
    //
    // G64 path
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
            d1541mem.getVIA2().clearMechBytePending();
            return;
        }

        gcrTrack.setTrackData(g64Image->getTrackData(g64TrackIndex));
        gcrTrack.setSpeedZones(g64Image->getTrackSpeedZones(g64TrackIndex));

        if (gcrTrack.empty())
        {
            gcrPos = 0;
            d1541mem.getVIA2().clearMechBytePending();
            return;
        }

        //
        // Build the sync map from the raw GCR data.
        //
        rebuildSyncMapForCurrentTrack();

        //
        // G64 does not give us a logical sector map.
        // Header sampling will update currentSector as the disk rotates.
        //
        gcrSectorAtPos.assign(gcrTrack.size(), currentSector);
        gcrWrittenMask.assign(gcrTrack.size(), 0);

        gcrPos %= gcrTrack.size();

        d1541mem.getVIA2().clearMechBytePending();

        return;
    }

    //
    // CBM sector-image path (D64)
    //
    if (currentTrack >= rawGcrTrackCache.size())
        return;

    if (rawGcrTrackValid[currentTrack])
    {
        gcrTrack.getTrackData()     = rawGcrTrackCache[currentTrack];
        gcrTrack.getSyncMap()       = rawGcrSyncCache[currentTrack];
        gcrSectorAtPos              = rawGcrSectorCache[currentTrack];

        if (gcrWrittenMask.size() != gcrTrack.size())
            gcrWrittenMask.assign(gcrTrack.size(), 0);

        if (!gcrTrack.empty())
            gcrPos %= gcrTrack.size();
        else
            gcrPos = 0;

        d1541mem.getVIA2().clearMechBytePending();
        return;
    }

    rebuildGCRTrackStream();

    rawGcrTrackCache[currentTrack]  = gcrTrack.getTrackData();
    rawGcrSyncCache[currentTrack]   = gcrTrack.getSyncMap();
    rawGcrSectorCache[currentTrack] = gcrSectorAtPos;
    rawGcrTrackValid[currentTrack]  = true;
    rawGcrTrackDirty[currentTrack]  = false;
}

void D1541::invalidateRawGcrCache()
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

void D1541::setDiskWriteGate(bool enabled)
{
    if (diskWriteGate == enabled)
        return;

    diskWriteGate = enabled;

    if (!enabled)
    {
        rebuildSyncMapForCurrentTrack();
        saveCurrentRawTrackToCache();

        writeGcrBuffer.clear();
        haveLastHeader = false;

        writeSyncRun = 0;
        writeAfterSync = false;
        writeGapRun = 0;

        pendingWritePos = 0;
        pendingWritePosValid = false;
    }
}

void D1541::sampleHeaderAtCurrentPosition(size_t pos)
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

    lastHeaderTrack = track;
    lastHeaderSector = sector;
    lastHeaderPos = pos;
    lastHeaderValid = true;
    haveLastHeader = true;

    if (getG64Image())
        currentSector = sector;
}

size_t D1541::findHeaderPosForSector(uint8_t track, uint8_t sector) const
{
    if (gcrTrack.empty())
        return SIZE_MAX;

    constexpr size_t HEADER_GCR_SIZE = 10;

    const size_t n = gcrTrack.size();

    for (size_t pos = 0; pos < n; ++pos)
    {
        const size_t prev = (pos + n - 1) % n;

        if (prev >= gcrTrack.getSyncMap().size() || gcrTrack.getSyncMap()[prev] == 0)
            continue;

        uint8_t gcrHeader[HEADER_GCR_SIZE];

        for (size_t i = 0; i < HEADER_GCR_SIZE; ++i)
            gcrHeader[i] = gcrTrack.getTrackData()[(pos + i) % n];

        std::vector<uint8_t> raw;
        raw.reserve(8);

        if (!gcrCodec.decodeBytes(gcrHeader, HEADER_GCR_SIZE, raw))
            continue;

        if (raw.size() != 8 || raw[0] != 0x08)
            continue;

        const uint8_t decodedSector = raw[2];
        const uint8_t decodedTrack  = raw[3];
        const uint8_t id2           = raw[4];
        const uint8_t id1           = raw[5];

        const uint8_t expectedChecksum = static_cast<uint8_t>(decodedSector ^ decodedTrack ^ id2 ^ id1);

        if (raw[1] != expectedChecksum)
            continue;

        if (decodedTrack == track && decodedSector == sector)
            return pos;
    }

    return SIZE_MAX;
}

void D1541::onVIA2PortARead(uint8_t value)
{
    readGcrHeaderProbe.push_back(value);

    constexpr size_t HEADER_GCR_SIZE = 10;

    if (readGcrHeaderProbe.size() < HEADER_GCR_SIZE)
        return;

    while (readGcrHeaderProbe.size() > HEADER_GCR_SIZE)
        readGcrHeaderProbe.erase(readGcrHeaderProbe.begin());

    std::vector<uint8_t> raw;
    raw.reserve(8);

    if (!gcrCodec.decodeBytes(readGcrHeaderProbe.data(), HEADER_GCR_SIZE, raw))
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

    lastRomHeaderTrack = track;
    lastRomHeaderSector = sector;
    lastRomHeaderValid = true;
    lastRomHeaderPos = gcrPos;
    lastRomHeaderCycle = 0;
}

void D1541::resetForMediaChange()
{
    // --- D1541-level flags ---
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

    // --- IMPORTANT: Drive/Peripheral protocol abort ---
    listening                   = false;
    talking                     = false;

    currentSecondaryAddress     = 0xFF;   // ensure “no channel selected”

    shiftReg                    = 0;
    bitsProcessed               = 0;

    // Clear handshake state so next LISTEN/TALK starts fresh
    waitingForAck               = false;
    ackEdgeCountdown            = 0;
    swallowPostHandshakeFalling = false;
    waitingForClkRelease        = false;
    prevClkLevel                = true;     // idle CLK high
    ackHold                     = false;
    byteAckHold                 = false;
    ackDelay                    = 0;

    status                      = DriveStatus::IDLE;

    // Clear any pending outgoing bytes
    while (!talkQueue.empty()) talkQueue.pop();

    currentDriveBusState = DriveBusState::IDLE;

    // --- VIA transient clears ---
    d1541mem.getVIA1().clearIECTransientState();
    d1541mem.getVIA2().clearMechBytePending();
    d1541mem.getVIA2().clearMechLatch();

    // --- Release actual line states ---
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
    lastHeaderTrack = 0;
    lastHeaderSector = 0;
    haveLastHeader = false;
    diskWriteGate = false;
    pendingWritePos = 0;
    pendingWritePosValid = false;
    trackModifiedByWrite = false;
    lastHeaderPos = 0;
    lastHeaderValid = false;
    readSyncRun = 0;
    readAfterSync = false;
    lastRomHeaderTrack = 0;
    lastRomHeaderSector = 0;
    lastRomHeaderValid = false;
    lastRomHeaderPos = 0;
    lastRomHeaderCycle = 0;
    writeSyncRun = 0;
    writeAfterSync = false;
    writeGapRun = 0;
    readGcrHeaderProbe.clear();
    gcrTrack.clear();
    gcrTrack.getSyncMap().clear();
    gcrSectorAtPos.clear();
    gcrWrittenMask.clear();
    writeGcrBuffer.clear();
    invalidateRawGcrCache();

    uint16_t pc = driveCPU.getPC();
    if (!(pc < 0x0800))
        driveCPU.reset();

    forceSyncIEC();
    updateIRQ();
}

void D1541::rebuildSyncMapForCurrentTrack()
{
    gcrTrack.rebuildSyncMap();
}

bool D1541::decodeRawSectorFromCurrentTrack(uint8_t track, uint8_t sector, std::vector<uint8_t>& outSector)
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

void D1541::flushCurrentRawTrackToImage()
{
    if (!diskLoaded || !diskImage)
        return;

    CBMImage* cbmImage = getCBMImage();

    if (!cbmImage)
        return;

    if (currentTrack >= rawGcrTrackDirty.size())
        return;

    if (!rawGcrTrackDirty[currentTrack])
        return;

    // Make sure current live raw track is cached first.
    saveCurrentRawTrackToCache();

    const uint8_t track1based = static_cast<uint8_t>(currentTrack + 1);
    const int spt = gcrCodec.sectorsPerTrack1541(track1based);

    int written = 0;
    int failed = 0;

    for (int sector = 0; sector < spt; ++sector)
    {
        std::vector<uint8_t> sectorBytes;

        if (!decodeRawSectorFromCurrentTrack(track1based, static_cast<uint8_t>(sector), sectorBytes))
        {
            ++failed;
            continue;
        }

        if (sectorBytes.size() != 256)
        {
            ++failed;
            continue;
        }

        if (cbmImage->writeSector(track1based, static_cast<uint8_t>(sector), sectorBytes))
            ++written;
        else
            ++failed;
    }

    rawGcrTrackDirty[currentTrack] = false;
}

void D1541::flushAllDirtyRawTracksToImage()
{
    if (!diskLoaded || !diskImage)
        return;

    // Save current live track before flushing.
    saveCurrentRawTrackToCache();

    const uint8_t oldTrack = currentTrack;
    const size_t oldPos = gcrPos;

    for (size_t t = 0; t < rawGcrTrackDirty.size(); ++t)
    {
        if (!rawGcrTrackDirty[t])
            continue;

        if (!rawGcrTrackValid[t])
            continue;

        currentTrack = static_cast<uint8_t>(t);

        gcrTrack.getTrackData() = rawGcrTrackCache[t];
        gcrTrack.getSyncMap()   = rawGcrSyncCache[t];
        gcrSectorAtPos          = rawGcrSectorCache[t];

        if (!gcrTrack.empty())
            gcrPos %= gcrTrack.size();
        else
            gcrPos = 0;

        flushCurrentRawTrackToImage();
    }

    currentTrack = oldTrack;
    gcrPos = oldPos;

    if (rawGcrTrackValid[currentTrack])
    {
        gcrTrack.getTrackData()     = rawGcrTrackCache[currentTrack];
        gcrTrack.getSyncMap()       = rawGcrSyncCache[currentTrack];
        gcrSectorAtPos              = rawGcrSectorCache[currentTrack];
    }

    gcrDirty = false;
}

CBMImage* D1541::getCBMImage()
{
    return dynamic_cast<CBMImage*>(diskImage.get());
}

const CBMImage* D1541::getCBMImage() const
{
    return dynamic_cast<const CBMImage*>(diskImage.get());
}

G64* D1541::getG64Image()
{
    return dynamic_cast<G64*>(diskImage.get());
}

const G64* D1541::getG64Image() const
{
    return dynamic_cast<const G64*>(diskImage.get());
}

Drive::IECSnapshot D1541::snapshotIEC() const
{
    Drive::IECSnapshot s{};

    s.atnLow            = getAtnLineLow();
    s.clkLow            = getClkLineLow();
    s.dataLow           = getDataLineLow();
    s.srqLow            = getSRQAsserted();

    s.drvAssertAtn      = assertAtn;
    s.drvAssertClk      = assertClk;
    s.drvAssertData     = assertData;
    s.drvAssertSrq      = assertSrq;

    // Protocol state
    s.busState          = currentDriveBusState;
    s.listening         = listening;
    s.talking           = talking;

    s.secondaryAddress  = this->currentSecondaryAddress;

    // Legacy shifter (from Peripheral)
    s.shiftReg          = shiftReg;
    s.bitsProcessed     = bitsProcessed;

    // Handshake + talk queue (from Drive)
    s.waitingForAck     = waitingForAck;
    s.ackEdgeCountdown  = ackEdgeCountdown;
    s.swallowPostHandshakeFalling = swallowPostHandshakeFalling;
    s.waitingForClkRelease = waitingForClkRelease;
    s.prevClkLevel      = prevClkLevel;
    s.ackHold           = ackHold;
    s.byteAckHold       = byteAckHold;
    s.ackDelay          = ackDelay;
    s.talkQueueLen      = talkQueue.size();

    return s;
}

void D1541::forceSyncIEC()
{
    if (iecBus)
    {
        atnLineLow  = !iecBus->getAtnLine();
        clkLineLow  = !iecBus->getClkLine();
        dataLineLow = !iecBus->getDataLine();
    }

    auto& via1 = d1541mem.getVIA1();
    via1.setIECInputLines(atnLineLow, clkLineLow, dataLineLow);

    iecLinesPrimed = true;
}

void D1541::flushAndSaveDisk()
{
    if (!diskImage || loadedDiskName.empty())
        return;

    if (getG64Image())
    {
        // Push the currently active raw G64 track back into the image.
        saveCurrentRawTrackToCache();
    }
    else
    {
        // D64/CBM image path.
        flushAllDirtyRawTracksToImage();
    }

    if (diskImage->isDirty())
    {
        if (diskImage->saveDisk(loadedDiskName))
            diskImage->clearDirty();
    }
}

void D1541::getDriveIndicators(std::vector<Indicator>& out) const
{
    out.clear();

    Indicator pwr;
    pwr.name = "PWR";
    pwr.on = isDiskLoaded();
    pwr.color = IDriveIndicatorView::DriveIndicatorColor::Green;
    out.push_back(std::move(pwr));

    Indicator act;
    act.name = "ACT";
    act.on = d1541mem.getVIA2().isLedOn();
    act.color = IDriveIndicatorView::DriveIndicatorColor::Red;
    out.push_back(std::move(act));
}
