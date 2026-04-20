// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Drive/D1541.h"

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
    driveCPU.attachMemoryInstance(&d1541mem);
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
    wrtr.writeU32(1);                  // version
    wrtr.writeU8(static_cast<uint8_t>(deviceNumber));

    // Dump the CPU state
    driveCPU.saveStatePayload(wrtr);
    driveCPU.saveStateExtendedPayload(wrtr);

    // 1541 mechanics + IEC + GCR position
    wrtr.writeBool(motorOn);
    wrtr.writeBool(diskLoaded);
    wrtr.writeBool(diskWriteProtected);
    wrtr.writeU8(currentTrack);
    wrtr.writeU8(currentSector);
    wrtr.writeU8(densityCode);
    wrtr.writeU16(halfTrackPos);
    wrtr.writeI32(gcrBitCounter);
    wrtr.writeU32(static_cast<uint32_t>(gcrPos));
    wrtr.writeBool(gcrDirty);

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

    // UI Activity State
    wrtr.writeU8(uiTrack);
    wrtr.writeU8(uiSector);
    wrtr.writeBool(uiLedWasOn);

    // Dump memory
    d1541mem.saveState(wrtr);

    // Dump VIA1
    d1541mem.getVIA1().saveState(wrtr);

    // Dump VIA2
    d1541mem.getVIA2().saveState(wrtr);

    wrtr.endChunk();
}

bool D1541::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "D541", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    // Header
    uint32_t ver = 0;
    if (!rdr.readU32(ver))                              { rdr.exitChunkPayload(chunk); return false; }
    if (ver != 1)                                       { rdr.exitChunkPayload(chunk); return false; }

    uint8_t dev = 0;
    if (!rdr.readU8(dev))                               { rdr.exitChunkPayload(chunk); return false; }
    deviceNumber = static_cast<int>(dev);

    // CPU (payload-only)
    if (!driveCPU.loadStatePayload(rdr))                { rdr.exitChunkPayload(chunk); return false; }
    if (!driveCPU.loadStateExtendedPayload(chunk, rdr)) { rdr.exitChunkPayload(chunk); return false; }

    // Mechanics / GCR
    if (!rdr.readBool(motorOn))                         { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(diskLoaded))                      { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(diskWriteProtected))              { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readU8(currentTrack))                      { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readU8(currentSector))                     { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readU8(densityCode))                       { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readI32(halfTrackPos))                     { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readI32(gcrBitCounter))                    { rdr.exitChunkPayload(chunk); return false; }

    uint32_t gp = 0;
    if (!rdr.readU32(gp))                               { rdr.exitChunkPayload(chunk); return false; }
    gcrPos = static_cast<size_t>(gp);

    if (!rdr.readBool(gcrDirty))                        { rdr.exitChunkPayload(chunk); return false; }

    // IEC protocol state
    if (!rdr.readBool(atnLineLow))                      { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(clkLineLow))                      { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(dataLineLow))                     { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(srqAsserted))                     { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readBool(iecLinesPrimed))                  { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(iecListening))                    { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(iecTalking))                      { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(presenceAckDone))                 { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readBool(expectingSecAddr))                { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(expectingDataByte))               { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readU8(currentListenSA))                   { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readU8(currentTalkSA))                     { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readBool(iecRxActive))                     { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readI32(iecRxBitCount))                    { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readU8(iecRxByte))                         { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readU8(uiTrack))                           { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readU8(uiSector))                          { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(uiLedWasOn))                      { rdr.exitChunkPayload(chunk); return false; }

    // Memory (payload-only)
    if (!d1541mem.loadState(rdr))                       { rdr.exitChunkPayload(chunk); return false; }

    // VIA1 / VIA2 (payload-only)
    if (!d1541mem.getVIA1().loadState(rdr))             { rdr.exitChunkPayload(chunk); return false; }
    if (!d1541mem.getVIA2().loadState(rdr))             { rdr.exitChunkPayload(chunk); return false; }

    // Done reading payload
    rdr.exitChunkPayload(chunk);

    // Post-load resync
    forceSyncIEC();

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

    if (bus)
    {
        bus->unTalk(deviceNumber);
        bus->unListen(deviceNumber);
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
    gcrTrackStream.clear();
    gcrSync.clear();
    gcrSectorAtPos.clear();
    gcrWrittenMask.clear();
    invalidateRawGcrCache();

    d1541mem.reset();
    driveCPU.reset();

    // UI activity
    uiTrack                     = currentTrack;
    uiSector                    = currentSector;
    uiLedWasOn                  = false;

    bool atnLow=false, clkLow=false, dataLow=false;
    if (bus)
    {
        // however your bus exposes the current resolved line states:
        atnLow  = !bus->getAtnLine();
        clkLow  = !bus->getClkLine();
        dataLow = !bus->getDataLine();
    }
    d1541mem.getVIA1().setIECInputLines(atnLow, clkLow, dataLow);
}

void D1541::tick(uint32_t cycles)
{
    int32_t remaining = cycles;

    while (remaining > 0)
    {
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

        if (!gcrTrackStream.empty())
            gcrPos = oldPos % gcrTrackStream.size();
        else
            gcrPos = 0;
    }

    if (gcrTrackStream.empty())
        return false;

    if (gcrSync.size() != gcrTrackStream.size())
        gcrSync.assign(gcrTrackStream.size(), 0);

    if (gcrSectorAtPos.size() != gcrTrackStream.size())
        gcrSectorAtPos.assign(gcrTrackStream.size(), currentSector);

    const size_t pos = gcrPos;

    const uint8_t gcrByte = gcrTrackStream[pos];
    const bool syncHigh   = (gcrSync[pos] != 0);
    const uint8_t sectorNow = gcrSectorAtPos[pos];

    currentSector = sectorNow;

    if (!diskWriteGate)
        sampleHeaderAtCurrentPosition(pos);

    gcrPos = (gcrPos + 1) % gcrTrackStream.size();

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
    // Use the VIA2 density latch as the source of truth for bit rate.
    const int cyclesPerByte = cyclesPerByteFromDensity(densityCode);

    gcrBitCounter += int(dc);

    while (gcrBitCounter >= cyclesPerByte)
    {
        gcrBitCounter -= cyclesPerByte;
        gcrTick();
    }
}

void D1541::rebuildGCRTrackStream()
{
    gcrTrackStream.clear();
    gcrSync.clear();
    gcrSectorAtPos.clear();

    if (!diskLoaded || !diskImage) return;

    const int track1based = int(currentTrack) + 1;
    const int spt = gcrCodec.sectorsPerTrack1541(track1based);

    auto bam = diskImage->readSector(18, 0);
    if (bam.size() < 256) bam.resize(256, 0x00);
    const uint8_t id1 = bam[0xA2];
    const uint8_t id2 = bam[0xA3];

    auto pushN = [&](uint8_t v, int count, bool isSync, uint8_t sectorTag)
    {
        gcrTrackStream.insert(gcrTrackStream.end(), count, v);
        gcrSync.insert(gcrSync.end(), count, isSync ? 1 : 0);
        gcrSectorAtPos.insert(gcrSectorAtPos.end(), count, sectorTag);
    };

    auto pushEncoded = [&](const uint8_t* in, size_t len, uint8_t sectorTag)
    {
        for (size_t i = 0; i < len; i += 4)
        {
            uint8_t g[5];
            gcrCodec.encode4Bytes(&in[i], g);
            gcrTrackStream.insert(gcrTrackStream.end(), g, g + 5);
            gcrSync.insert(gcrSync.end(), 5, 0);
            gcrSectorAtPos.insert(gcrSectorAtPos.end(), 5, sectorTag);
        }
    };

    // Same “DOS-ish defaults” as D1571
    constexpr int SYNC_LEN   = 10;
    constexpr int HEADER_GAP = 9;
    constexpr int TAIL_GAP   = 9;

    // Lead-in gap (NOT sync)
    pushN(0x55, 64, false, 0);

    for (int sector = 0; sector < spt; ++sector)
    {
        std::vector<uint8_t> sec = diskImage->readSector(uint8_t(track1based), uint8_t(sector));
        if (sec.size() != 256) sec.assign(256, 0x00);

        // ---- HEADER ----
        pushN(0xFF, SYNC_LEN, true, uint8_t(sector));

        uint8_t hdr[8] = {0};
        hdr[0] = 0x08;
        hdr[2] = uint8_t(sector);     // sector
        hdr[3] = uint8_t(track1based);// track
        hdr[4] = id2;
        hdr[5] = id1;
        hdr[6] = 0x0F;
        hdr[7] = 0x0F;
        hdr[1] = uint8_t(hdr[2] ^ hdr[3] ^ hdr[4] ^ hdr[5]); // header checksum

        pushEncoded(hdr, 8, uint8_t(sector));
        pushN(0x55, HEADER_GAP, false, uint8_t(sector));

        // ---- DATA ----
        pushN(0xFF, SYNC_LEN, true, uint8_t(sector));

        std::vector<uint8_t> raw(260, 0x00);
        raw[0] = 0x07; // data block ID

        uint8_t csum = 0;
        for (int i = 0; i < 256; ++i)
        {
            raw[1 + i] = sec[i];
            csum ^= raw[1 + i];
        }
        raw[257] = csum;
        raw[258] = 0x00;
        raw[259] = 0x00;

        pushEncoded(raw.data(), raw.size(), uint8_t(sector));
        pushN(0x55, TAIL_GAP, false, uint8_t(sector));
    }

    // Trailing gap
    pushN(0x55, 128, false, 0);

    // Sanity
    if (gcrSync.size() != gcrTrackStream.size())
        gcrSync.assign(gcrTrackStream.size(), 0);

    gcrPos = 0;

    gcrWrittenMask.assign(gcrTrackStream.size(), 0);
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

    diskWriteProtected  = false;

    auto img            = DiskFactory::create(path);
    if (!img || !img->loadDisk(path))
    {
        // "Door open / no media" behavior: don't reset the drive computer
        loadedDiskName.clear();
        diskImage.reset();
        diskLoaded      = false;
        lastError       = DriveError::NO_DISK;

        // Invalidate any ongoing media stream
        gcrDirty        = true;
        gcrPos          = 0;
        gcrBitCounter   = 0;

        gcrTrackStream.clear();
        gcrSync.clear();
        gcrSectorAtPos.clear();
        d1541mem.getVIA2().clearMechBytePending();
        return;
    }

    // HOT SWAP
    diskImage           = std::move(img);
    diskLoaded          = true;
    invalidateRawGcrCache();
#ifdef Debug
    debugDumpDirectorySectors("after-load");
#endif
    loadedDiskName      = path;
    status              = DriveStatus::READY;
    lastError           = DriveError::NONE;

    // Invalidate/rebuild media stream for the newly inserted disk
    gcrDirty            = true;
    gcrPos              = 0;
    gcrBitCounter       = 0;

    gcrTrackStream.clear();
    gcrSync.clear();
    gcrSectorAtPos.clear();
    d1541mem.getVIA2().clearMechBytePending();
}

void D1541::unloadDisk()
{
    // Save any file changes to disk first
    if (diskImage && diskImage->isDirty() && !loadedDiskName.empty())
    {
        diskImage->saveDisk(loadedDiskName);
        diskImage->clearDirty();
    }

    diskImage.reset();  // Reset disk image by assigning a fresh instance
    loadedDiskName.clear();

    gcrPos = 0;
    gcrBitCounter = 0;
    gcrTrackStream.clear();
    gcrSync.clear();
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

    if (!pendingWritePosValid || gcrTrackStream.empty())
        return;

    const size_t pos = pendingWritePos % gcrTrackStream.size();

    gcrTrackStream[pos] = value;

    // Rebuild sync state around the byte we just wrote.
    //
    // Do NOT treat a single $FF byte as a full sync region.
    // A real sync mark is a run of 1 bits. At byte granularity this is
    // approximated as a short run of consecutive $FF bytes.
    if (gcrSync.size() == gcrTrackStream.size())
    {
        const size_t n = gcrTrackStream.size();

        for (int rel = -16; rel <= 16; ++rel)
        {
            const size_t p = (pos + n + rel) % n;

            int ffRun = 0;

            for (int back = 0; back < 12; ++back)
            {
                const size_t q = (p + n - static_cast<size_t>(back)) % n;

                if (gcrTrackStream[q] == 0xFF)
                    ++ffRun;
                else
                    break;
            }

            gcrSync[p] = (ffRun >= 2) ? 1 : 0;
        }
    }

    if (gcrWrittenMask.size() == gcrTrackStream.size())
        gcrWrittenMask[pos] = 1;

    trackModifiedByWrite = true;

    // Keep the cached raw track up to date so the ROM can verify what it just wrote.
    saveCurrentRawTrackToCache();

    acceptGCRWriteByte(value);
}

void D1541::acceptGCRWriteByte(uint8_t value)
{
    writeGcrBuffer.push_back(value);

    // Keep bounded so noise does not grow forever.
    if (writeGcrBuffer.size() > 4096)
    {
        writeGcrBuffer.erase(writeGcrBuffer.begin(),
                             writeGcrBuffer.begin() + 1024);
    }

    #ifdef Debug
    static int writeByteLogCount = 0;

    if ((writeByteLogCount++ % 64) == 0)
    {
        std::cout << "[D1541:GCR-WRITE] sample $"
                  << std::hex << std::uppercase << int(value)
                  << std::dec
                  << " T" << int(currentTrack + 1)
                  << " S" << int(currentSector)
                  << "\n";
    }
    #endif

    // Do not decode/commit to the D64 sector image here. The raw GCR track is
    // authoritative while the disk is mounted. A later safe flush/decode step
    // can persist dirty raw tracks back to the sector image.
}

void D1541::tryDecodeWrittenGCR()
{
    if (!diskImage)
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

        const uint8_t expectedChecksum =
            static_cast<uint8_t>(sector ^ track ^ id2 ^ id1);

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

                #ifdef Debug
                    std::cout << "[D1541:GCR-WRITE] header T"
                              << int(lastHeaderTrack)
                              << " S"
                              << int(lastHeaderSector)
                              << "\n";
                #endif

                writeGcrBuffer.erase(writeGcrBuffer.begin(),
                                     writeGcrBuffer.begin() + pos + HEADER_GCR_SIZE);

                madeProgress = true;
                break;
            }

            std::vector<uint8_t> sectorData;

            if (haveLastHeader && decodeDataAt(pos, sectorData))
            {
                if (lastHeaderTrack >= 1 &&
                    lastHeaderTrack <= 35 &&
                    lastHeaderSector < gcrCodec.sectorsPerTrack1541(lastHeaderTrack))
                {
                    diskImage->writeSector(lastHeaderTrack, lastHeaderSector, sectorData);

                    // Rebuild generated GCR stream from the updated sector data.
                    gcrDirty = true;

                #ifdef Debug
                    std::cout << "[D1541:GCR-WRITE] committed T"
                              << int(lastHeaderTrack)
                              << " S"
                              << int(lastHeaderSector)
                              << "\n";
                #endif
                }

                writeGcrBuffer.erase(writeGcrBuffer.begin(),
                                     writeGcrBuffer.begin() + pos + DATA_GCR_SIZE);

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

    halfTrackPos = std::clamp(halfTrackPos + step, 0, 34 * 2);  // 0..68 halftracks
    currentTrack = uint8_t(halfTrackPos / 2);                   // 0..34 (=> track 1..35)

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
    if (currentTrack >= rawGcrTrackCache.size())
        return;

    if (gcrTrackStream.empty())
        return;

    rawGcrTrackCache[currentTrack]  = gcrTrackStream;
    rawGcrSyncCache[currentTrack]   = gcrSync;
    rawGcrSectorCache[currentTrack] = gcrSectorAtPos;
    rawGcrTrackValid[currentTrack]  = true;

    if (trackModifiedByWrite)
        rawGcrTrackDirty[currentTrack] = true;
}

void D1541::loadCurrentRawTrackFromCacheOrBuild()
{
    if (currentTrack >= rawGcrTrackCache.size())
        return;

    if (rawGcrTrackValid[currentTrack])
    {
        gcrTrackStream = rawGcrTrackCache[currentTrack];
        gcrSync        = rawGcrSyncCache[currentTrack];
        gcrSectorAtPos = rawGcrSectorCache[currentTrack];

        if (gcrWrittenMask.size() != gcrTrackStream.size())
            gcrWrittenMask.assign(gcrTrackStream.size(), 0);

        if (!gcrTrackStream.empty())
            gcrPos %= gcrTrackStream.size();
        else
            gcrPos = 0;

        d1541mem.getVIA2().clearMechBytePending();
        return;
    }

    rebuildGCRTrackStream();

    rawGcrTrackCache[currentTrack]  = gcrTrackStream;
    rawGcrSyncCache[currentTrack]   = gcrSync;
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

    gcrTrackStream.clear();
    gcrSync.clear();
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

#ifdef Debug
    std::cout << "[D1541:WRITE-GATE] "
              << (enabled ? "ON" : "OFF")
              << " PC=$"
              << std::hex << std::uppercase << driveCPU.getPC()
              << std::dec
              << " pos=" << gcrPos
              << " T" << int(currentTrack + 1)
              << " S" << int(currentSector);

    if (lastHeaderValid && !gcrTrackStream.empty())
    {
        const size_t delta =
            (gcrPos + gcrTrackStream.size() - lastHeaderPos) %
            gcrTrackStream.size();

        std::cout << " passiveHeader=T"
                  << int(lastHeaderTrack)
                  << " S" << int(lastHeaderSector)
                  << " headerPos=" << lastHeaderPos
                  << " delta=" << delta;
    }
    else
    {
        std::cout << " passiveHeader=<none>";
    }

    if (lastRomHeaderValid && !gcrTrackStream.empty())
    {
        const size_t romDelta =
            (gcrPos + gcrTrackStream.size() - lastRomHeaderPos) %
            gcrTrackStream.size();

        std::cout << " romHeader=T"
                  << int(lastRomHeaderTrack)
                  << " S" << int(lastRomHeaderSector)
                  << " romPos=" << lastRomHeaderPos
                  << " romDelta=" << romDelta;
    }
    else
    {
        std::cout << " romHeader=<none>";
    }

    std::cout << "\n";

    if (enabled)
    {
        debugDumpWriteContext("gate-on");

        const uint8_t targetTrack  = d1541mem.read(0x0A);
        const uint8_t targetSector = d1541mem.read(0x0B);
        const size_t targetHeaderPos = findHeaderPosForSector(targetTrack, targetSector);

        std::cout << "[D1541:WRITE-PHASE] target=T"
                  << int(targetTrack)
                  << " S" << int(targetSector)
                  << " gatePos=" << gcrPos;

        if (targetHeaderPos != SIZE_MAX && !gcrTrackStream.empty())
        {
            const size_t delta =
                (gcrPos + gcrTrackStream.size() - targetHeaderPos) %
                gcrTrackStream.size();

            std::cout << " targetHeaderPos=" << targetHeaderPos
                      << " deltaFromTargetHeader=" << delta;
        }
        else
        {
            std::cout << " targetHeaderPos=<not found>";
        }

        std::cout << " currentTag=T"
                  << int(currentTrack + 1)
                  << " S" << int(currentSector)
                  << "\n";
    }
#endif

    if (!enabled)
    {
        rebuildSyncMapForCurrentTrack();
        saveCurrentRawTrackToCache();

        #ifdef Debug
        debugVerifyRawSector(18, 1);
        #endif

        #ifdef Debug
        const bool rawOk = debugVerifyRawSector(18, 1);

        if (!rawOk && currentTrack == 17)
        {
            std::cout << "[D1541:WRITE-ROLLBACK] raw T18 failed verify; rebuilding track from image\n";

            if (currentTrack < rawGcrTrackValid.size())
            {
                rawGcrTrackValid[currentTrack] = false;
                rawGcrTrackDirty[currentTrack] = false;
                rawGcrTrackCache[currentTrack].clear();
                rawGcrSyncCache[currentTrack].clear();
                rawGcrSectorCache[currentTrack].clear();
            }

            gcrDirty = true;
        }
        #endif

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
    if (gcrTrackStream.empty())
        return;

    constexpr size_t HEADER_GCR_SIZE = 10;

    if (pos + HEADER_GCR_SIZE > gcrTrackStream.size())
        return;

    if (pos == 0)
        return;

    const size_t prev = pos - 1;

    if (prev >= gcrSync.size() || gcrSync[prev] == 0)
        return;

    std::vector<uint8_t> raw;
    raw.reserve(8);

    if (!gcrCodec.decodeBytes(&gcrTrackStream[pos], HEADER_GCR_SIZE, raw))
        return;

    if (raw.size() != 8 || raw[0] != 0x08)
        return;

    const uint8_t sector = raw[2];
    const uint8_t track  = raw[3];
    const uint8_t id2    = raw[4];
    const uint8_t id1    = raw[5];

    const uint8_t expectedChecksum =
        static_cast<uint8_t>(sector ^ track ^ id2 ^ id1);

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
}

size_t D1541::findHeaderPosForSector(uint8_t track, uint8_t sector) const
{
    if (gcrTrackStream.empty())
        return SIZE_MAX;

    constexpr size_t HEADER_GCR_SIZE = 10;

    for (size_t pos = 1; pos + HEADER_GCR_SIZE <= gcrTrackStream.size(); ++pos)
    {
        const size_t prev = pos - 1;

        if (prev >= gcrSync.size() || gcrSync[prev] == 0)
            continue;

        std::vector<uint8_t> raw;
        raw.reserve(8);

        if (!gcrCodec.decodeBytes(&gcrTrackStream[pos], HEADER_GCR_SIZE, raw))
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

    const uint8_t expectedChecksum =
        static_cast<uint8_t>(sector ^ track ^ id2 ^ id1);

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

#ifdef Debug
void D1541::debugDumpDirectorySectors(const char* tag)
{
    if (!diskImage)
        return;

    auto dumpSector = [&](uint8_t sector)
    {
        auto sec = diskImage->readSector(18, sector);
        if (sec.size() < 256)
            sec.resize(256, 0x00);

        std::cout << "[D1541:DIR-DUMP] " << tag
                  << " T18 S" << int(sector)
                  << " link=" << int(sec[0]) << "/" << int(sec[1])
                  << " first entry type=$"
                  << std::hex << std::uppercase << int(sec[2])
                  << std::dec
                  << " start=" << int(sec[3]) << "/" << int(sec[4])
                  << " first 16:";

        for (int i = 0; i < 16; ++i)
        {
            std::cout << " $"
                      << std::hex << std::uppercase << int(sec[i])
                      << std::dec;
        }

        std::cout << "\n";
    };

    dumpSector(0);
    dumpSector(1);
    dumpSector(4);
}

void D1541::debugDumpWriteContext(const char* tag)
{
    std::cout << "[D1541:WRITE-CTX] " << tag
              << " PC=$" << std::hex << std::uppercase << driveCPU.getPC()
              << std::dec
              << " pos=" << gcrPos
              << " cur=T" << int(currentTrack + 1)
              << "/S" << int(currentSector)
              << "\n";

    std::cout << "  $00-$1F:";
    for (uint16_t a = 0x0000; a <= 0x001F; ++a)
    {
        std::cout << " $"
                  << std::hex << std::uppercase << int(d1541mem.read(a))
                  << std::dec;
    }
    std::cout << "\n";
}

void D1541::debugDumpGcrWindow(const char* tag, size_t center, int before, int after)
{
    if (gcrTrackStream.empty())
        return;

    const size_t n = gcrTrackStream.size();

    std::cout << "[D1541:GCR-WINDOW] " << tag
              << " center=" << center
              << " size=" << n
              << "\n  ";

    for (int i = -before; i <= after; ++i)
    {
        const size_t p = (center + n + i) % n;

        if (i == 0)
            std::cout << " |";

        std::cout << " $"
                  << std::hex << std::uppercase
                  << int(gcrTrackStream[p])
                  << std::dec;

        if (i == 0)
            std::cout << "|";
    }

    std::cout << "\n";
}
#endif

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
    if (bus)
    {
        bus->unTalk(deviceNumber);
        bus->unListen(deviceNumber);
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
    gcrTrackStream.clear();
    gcrSync.clear();
    gcrSectorAtPos.clear();
    gcrWrittenMask.clear();
    writeGcrBuffer.clear();
    invalidateRawGcrCache();

    uint16_t pc = driveCPU.getPC();
    if (!(pc < 0x0800))
        driveCPU.reset();

    forceSyncIEC();
}

void D1541::rebuildSyncMapForCurrentTrack()
{
    if (gcrTrackStream.empty())
        return;

    gcrSync.assign(gcrTrackStream.size(), 0);

    const size_t n = gcrTrackStream.size();

    for (size_t p = 0; p < n; ++p)
    {
        int ffRun = 0;

        for (int back = 0; back < 12; ++back)
        {
            const size_t q = (p + n - size_t(back)) % n;

            if (gcrTrackStream[q] == 0xFF)
                ++ffRun;
            else
                break;
        }

        if (ffRun >= 2)
            gcrSync[p] = 1;
    }
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
    // Push current line states into VIA even if nothing changed
    auto& via1 = d1541mem.getVIA1();
    via1.setIECInputLines(atnLineLow, clkLineLow, dataLineLow);
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

bool D1541::debugVerifyRawSector(uint8_t track, uint8_t sector)
{
#ifdef Debug
    if (gcrTrackStream.empty())
        return false;

    const size_t headerPos = findHeaderPosForSector(track, sector);
    if (headerPos == SIZE_MAX)
    {
        std::cout << "[D1541:VERIFY-RAW] T"
                  << int(track) << " S" << int(sector)
                  << " header not found\n";
        return false;
    }

    const size_t n = gcrTrackStream.size();

    constexpr size_t DATA_GCR_SIZE = 325;

    // Try a range of possible starts after the header. The ROM may start
    // write-gate before the generated data sync, and it writes its own sync/data.
    const size_t scanStart = (headerPos + 10) % n;

    bool anyDecoded = false;

    for (size_t offset = 0; offset < 96; ++offset)
    {
        const size_t dataStart = (scanStart + offset) % n;

        std::vector<uint8_t> gcrBlock;
        gcrBlock.reserve(DATA_GCR_SIZE);

        for (size_t i = 0; i < DATA_GCR_SIZE; ++i)
            gcrBlock.push_back(gcrTrackStream[(dataStart + i) % n]);

        std::vector<uint8_t> raw;
        raw.reserve(260);

        const bool decoded =
            gcrCodec.decodeBytes(gcrBlock.data(), DATA_GCR_SIZE, raw);

        if (!decoded || raw.size() != 260)
            continue;

        anyDecoded = true;

        uint8_t checksum = 0;
        for (int i = 0; i < 256; ++i)
            checksum ^= raw[1 + i];

        const bool idOk = (raw[0] == 0x07);
        const bool checksumOk = (checksum == raw[257]);

        std::cout << "[D1541:VERIFY-RAW] T"
                  << int(track) << " S" << int(sector)
                  << " headerPos=" << headerPos
                  << " dataStart=" << dataStart
                  << " offset=" << offset
                  << " blockId=$" << std::hex << std::uppercase << int(raw[0])
                  << " checksum=$" << int(raw[257])
                  << " calc=$" << int(checksum)
                  << std::dec
                  << " idOk=" << (idOk ? 1 : 0)
                  << " checksumOk=" << (checksumOk ? 1 : 0)
                  << " firstData=$"
                  << std::hex << std::uppercase
                  << int(raw[1]) << " "
                  << int(raw[2]) << " "
                  << int(raw[3]) << " "
                  << int(raw[4])
                  << std::dec
                  << "\n";

        if (idOk && checksumOk)
            return true;
    }

    std::cout << "[D1541:VERIFY-RAW] T"
              << int(track) << " S" << int(sector)
              << " no valid 325-byte GCR window found after headerPos="
              << headerPos
              << " anyDecoded=" << (anyDecoded ? 1 : 0)
              << "\n";

    return false;
#else
    return false;
#endif
}
