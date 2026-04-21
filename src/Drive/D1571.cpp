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
    halfTrackPos(18 * 2),
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
    driveCPU.attachIRQLineInstance(&IRQ);
    driveCPU.attachMemoryInstance(&d1571mem);

    if (!d1571mem.initialize(romName))
    {
        throw std::runtime_error("Unable to start drive, ROM not loaded!\n");
    }

    reset();
}

D1571::~D1571() = default;

void D1571::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("D157");
    wrtr.writeU32(1);
    wrtr.writeU8(static_cast<uint8_t>(deviceNumber));
    wrtr.writeU8(static_cast<uint8_t>(mediaPath));

    // Dump the CPU state
    driveCPU.saveStatePayload(wrtr);
    driveCPU.saveStateExtendedPayload(wrtr);

    // Mechanics / runtime state
    wrtr.writeBool(motorOn);
    wrtr.writeBool(diskLoaded);
    wrtr.writeBool(diskWriteProtected);

    wrtr.writeU8(static_cast<uint8_t>(lastError));
    wrtr.writeU8(static_cast<uint8_t>(status));

    wrtr.writeU8(currentTrack);
    wrtr.writeU8(currentSector);
    wrtr.writeU8(densityCode);

    wrtr.writeBool(currentSide);
    wrtr.writeU16(static_cast<uint16_t>(halfTrackPos));

    // Disk attachment info
    wrtr.writeString(loadedDiskName);

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

    // GCR resume state (bit-exact)
    wrtr.writeU32(static_cast<uint32_t>(gcrBitCounter));
    wrtr.writeU32(static_cast<uint32_t>(gcrPos));
    wrtr.writeBool(gcrDirty);

    // UI Activity State
    wrtr.writeU8(uiTrack);
    wrtr.writeU8(uiSector);
    wrtr.writeBool(uiLedWasOn);

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
    if (ver != 1)                                           { rdr.exitChunkPayload(chunk); return false; }

    uint8_t devU8 = 0;
    if (!rdr.readU8(devU8))                                 { rdr.exitChunkPayload(chunk); return false; }
    setDeviceNumber(static_cast<int>(devU8));

    uint8_t mediaU8 = 0;
    if (!rdr.readU8(mediaU8))                               { rdr.exitChunkPayload(chunk); return false; }
    mediaPath = static_cast<MediaPath>(mediaU8);

    // CPU state (must match save order)
    if (!driveCPU.loadStatePayload(rdr))                    { rdr.exitChunkPayload(chunk); return false; }
    if (!driveCPU.loadStateExtendedPayload(chunk, rdr))     { rdr.exitChunkPayload(chunk); return false; }

    // Mechanics / runtime state
    if (!rdr.readBool(motorOn))                             { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(diskLoaded))                          { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(diskWriteProtected))                  { rdr.exitChunkPayload(chunk); return false; }

    uint8_t u8 = 0;

    if (!rdr.readU8(u8))                                    { rdr.exitChunkPayload(chunk); return false; }
    lastError = static_cast<DriveError>(u8);

    if (!rdr.readU8(u8))                                    { rdr.exitChunkPayload(chunk); return false; }
    status = static_cast<DriveStatus>(u8);

    if (!rdr.readU8(currentTrack))                          { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readU8(currentSector))                         { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readU8(densityCode))                           { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readBool(currentSide))                         { rdr.exitChunkPayload(chunk); return false; }

    uint16_t ht = 0;
    if (!rdr.readU16(ht))                                   { rdr.exitChunkPayload(chunk); return false; }
    halfTrackPos = static_cast<int>(ht);

    // Disk attachment info
    if (!rdr.readString(loadedDiskName))                    { rdr.exitChunkPayload(chunk); return false; }

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
    uint32_t tmp32 = 0;

    if (!rdr.readU32(tmp32))                                { rdr.exitChunkPayload(chunk); return false; }
    gcrBitCounter = static_cast<int>(tmp32);

    if (!rdr.readU32(tmp32))                                { rdr.exitChunkPayload(chunk); return false; }
    gcrPos = static_cast<size_t>(tmp32);

    if (!rdr.readBool(gcrDirty))                            { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readU8(uiTrack))                               { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readU8(uiSector))                              { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(uiLedWasOn))                          { rdr.exitChunkPayload(chunk); return false; }

    // Memory + VIAs (match save order)
    if (!d1571mem.loadState(rdr))                           { rdr.exitChunkPayload(chunk); return false; }
    if (!d1571mem.getVIA1().loadState(rdr))                 { rdr.exitChunkPayload(chunk); return false; }
    if (!d1571mem.getVIA2().loadState(rdr))                 { rdr.exitChunkPayload(chunk); return false; }

    // Post-restore fixups (IMPORTANT for deterministic resume)

    // We do NOT serialize gcrTrackStream/gcrSync (huge). Rebuild lazily.
    gcrTrackStream.clear();
    gcrSync.clear();
    gcrSectorAtPos.clear();

    // If a disk is expected, ensure rebuild will happen next gcrTick().
    if (diskLoaded)
        gcrDirty = true;

    // Sync VIA1 input pins to the bus line levels we restored
    forceSyncIEC();

    // Bring SRQ output in sync with restored state
    peripheralAssertSrq(srqAsserted);

    // IRQ line derived from VIA/CIA/FDC state
    updateIRQ();

    return true;
}

void D1571::tick(uint32_t cycles)
{
    while (cycles > 0)
    {
        driveCPU.tick();

        uint32_t dc = driveCPU.getElapsedCycles();
        if (dc == 0) dc = 1;
        if (dc > cycles) dc = cycles;

        // Tick “hardware time”
        d1571mem.tick(dc);
        Drive::tick(dc);

        if (atnLineLow) peripheralAssertClk(false);

        if (isGCRMode() && motorOn && diskLoaded)
            gcrAdvance(dc);

        const bool ledOn = d1571mem.getVIA2().isLedOn();

        if (ledOn)
            uiTrack = currentTrack;

        if (ledOn && !uiLedWasOn)
        {
            uiSector = currentSector;
        }

        uiLedWasOn = ledOn;

        cycles -= dc;
    }
}

bool D1571::gcrTick()
{
    if (gcrDirty)
    {
        size_t oldPos = gcrPos;
        rebuildGCRTrackStream();
        gcrDirty = false;

        if (!gcrTrackStream.empty())
            gcrPos = oldPos % gcrTrackStream.size();
        else
            gcrPos = 0;
    }

    if (gcrTrackStream.empty()) return false;
    if (gcrSync.size() != gcrTrackStream.size())
        gcrSync.assign(gcrTrackStream.size(), 0);

    const size_t pos = gcrPos;

    if (diskWriteGate)
    {
        pendingWritePos = pos;
        pendingWritePosValid = true;

        auto& via2 = d1571mem.getVIA2();
        via2.pulseWriteByteReady();

        gcrPos = (gcrPos + 1) % gcrTrackStream.size();
        return true;
    }

    uint8_t gcrByte = gcrTrackStream[pos];
    bool syncHigh = (gcrSync[pos] != 0);

    if (gcrSectorAtPos.size() == gcrTrackStream.size())
    {
        currentSector = gcrSectorAtPos[pos];
    }

    gcrPos = (gcrPos + 1) % gcrTrackStream.size();

    d1571mem.getVIA2().diskByteFromMedia(gcrByte, syncHigh);
    return true;
}

void D1571::gcrAdvance(uint32_t dc)
{
    // Map 1571 density code (0..3) to an approximate
    // "CPU cycles per GCR byte".
    int cyclesPerByte;
    switch (densityCode & 0x03)
    {
        case 0:  cyclesPerByte = 26; break; // fastest
        case 1:  cyclesPerByte = 28; break;
        case 2:  cyclesPerByte = 30; break;
        default: cyclesPerByte = 32; break; // slowest
    }

    gcrBitCounter += dc;

    while (gcrBitCounter >= cyclesPerByte)
    {
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

    if (bus)
    {
        bus->unTalk(deviceNumber);
        bus->unListen(deviceNumber);
    }

    gcrTrackStream.clear();
    gcrSync.clear();
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
    // Push current line states into VIA even if nothing changed
    auto& via1 = d1571mem.getVIA1();
    via1.setIECInputLines(atnLineLow, clkLineLow, dataLineLow);
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
    if (mediaPath == MediaPath::GCR_D64)
    {
        if (currentSide != 0)
        {
            saveCurrentRawTrackToCache();

            currentSide = 0;

            // Force rebuild, but preserve rotational position.
            gcrTrackStream.clear();
            gcrSync.clear();
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

        // Changing sides changes the disk surface under the head,
        // but NOT the physical cylinder and NOT the rotational position.
        //
        // Do not reset gcrPos here.
        // gcrTick() will save oldPos, rebuild the track, then remap oldPos
        // onto the new side's GCR stream.
        gcrTrackStream.clear();
        gcrSync.clear();
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
    gcrTrackStream.clear();
    gcrSync.clear();
    gcrSectorAtPos.clear();

    if (!diskLoaded || !diskImage) return;

    const size_t cacheTrack = currentRawCacheIndex();

    if (cacheTrack < rawGcrTrackValid.size() && rawGcrTrackValid[cacheTrack])
    {
        gcrTrackStream = rawGcrTrackCache[cacheTrack];
        gcrSync        = rawGcrSyncCache[cacheTrack];
        gcrSectorAtPos = rawGcrSectorCache[cacheTrack];

        if (!gcrTrackStream.empty())
            gcrPos %= gcrTrackStream.size();
        else
            gcrPos = 0;

        d1571mem.getVIA2().clearMechBytePending();
        return;
    }

    const int trackOnSide1based = int(currentTrackOnSide1Based());
    const int imageTrack1based  = int(currentImageTrack1Based());
    const int spt = sectorsPerTrack1541(trackOnSide1based);

    auto bam = diskImage->readSector(18, 0);
    #ifdef Debug
    std::cout << "[BAM] 18/0 first bytes: "
              << std::hex
              << int(bam[0]) << " " << int(bam[1]) << " " << int(bam[2]) << " " << int(bam[3])
              << std::dec << "\n";
    #endif
    if (bam.size() < 256) bam.resize(256, 0x00);
    uint8_t id1 = bam[0xA2];
    uint8_t id2 = bam[0xA3];

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
            gcrEncode4Bytes(&in[i], g);

            gcrTrackStream.insert(gcrTrackStream.end(), g, g + 5);
            gcrSync.insert(gcrSync.end(), 5, 0);
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
        std::vector<uint8_t> sec = diskImage->readSector(uint8_t(imageTrack1based), uint8_t(sectorTag));
        #ifdef Debug
        if (!currentSide && imageTrack1based == 18 && sector == 1)
        {
            std::cout << "[DIR] T18 S1 link=" << int(sec[0]) << "/" << int(sec[1])
                      << " type=$" << std::hex << int(sec[2]) << std::dec << "\n";

            std::cout << "      first 32 bytes: ";
            for (int i = 0; i < 32; i++) std::cout << std::hex << int(sec[i]) << " ";
            std::cout << std::dec << "\n";
        }
        #endif
        if (sec.size() != 256) sec.assign(256, 0x00);

        // ---- HEADER ----
        pushN(0xFF, SYNC_LEN, true, sectorTag);        // sync region

        uint8_t hdr[8] = {0};
        hdr[0] = 0x08;
        hdr[2] = uint8_t(sector);            // Byte 2 is Sector
        hdr[3] = uint8_t(trackOnSide1based); // Physical track on selected side
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
    if (gcrSync.size() != gcrTrackStream.size())
        gcrSync.assign(gcrTrackStream.size(), 0);

    if (gcrSectorAtPos.size() != gcrTrackStream.size())
        gcrSectorAtPos.assign(gcrTrackStream.size(), currentSector);

    // Reset
    gcrPos = 0;

    d1571mem.getVIA2().clearMechBytePending();
    saveCurrentRawTrackToCache();
}

void D1571::gcrEncode4Bytes(const uint8_t in[4], uint8_t out[5])
{
    uint8_t n[8] = {
        uint8_t(in[0] >> 4), uint8_t(in[0] & 0x0F),
        uint8_t(in[1] >> 4), uint8_t(in[1] & 0x0F),
        uint8_t(in[2] >> 4), uint8_t(in[2] & 0x0F),
        uint8_t(in[3] >> 4), uint8_t(in[3] & 0x0F),
    };

    uint64_t bits = 0;
    for (int i = 0; i < 8; i++)
        bits = (bits << 5) | (GCR5[n[i]] & 0x1F);

    out[0] = uint8_t((bits >> 32) & 0xFF);
    out[1] = uint8_t((bits >> 24) & 0xFF);
    out[2] = uint8_t((bits >> 16) & 0xFF);
    out[3] = uint8_t((bits >>  8) & 0xFF);
    out[4] = uint8_t((bits >>  0) & 0xFF);
}

void D1571::gcrEncodeBytes(const uint8_t* in, size_t len, std::vector<uint8_t>& out)
{
    for (size_t i = 0; i < len; i += 4)
    {
        uint8_t g[5];
        gcrEncode4Bytes(&in[i], g);
        out.insert(out.end(), g, g + 5);
    }
}

bool D1571::gcrDecodeBytes(const uint8_t* in, size_t len, std::vector<uint8_t>& out) const
{
    out.clear();

    if (!in)
        return false;

    if ((len % 5) != 0)
        return false;

    auto decode5 = [](uint8_t code, uint8_t& nibble) -> bool
    {
        switch (code & 0x1F)
        {
            case 0x0A: nibble = 0x0; return true;
            case 0x0B: nibble = 0x1; return true;
            case 0x12: nibble = 0x2; return true;
            case 0x13: nibble = 0x3; return true;
            case 0x0E: nibble = 0x4; return true;
            case 0x0F: nibble = 0x5; return true;
            case 0x16: nibble = 0x6; return true;
            case 0x17: nibble = 0x7; return true;
            case 0x09: nibble = 0x8; return true;
            case 0x19: nibble = 0x9; return true;
            case 0x1A: nibble = 0xA; return true;
            case 0x1B: nibble = 0xB; return true;
            case 0x0D: nibble = 0xC; return true;
            case 0x1D: nibble = 0xD; return true;
            case 0x1E: nibble = 0xE; return true;
            case 0x15: nibble = 0xF; return true;
            default:   return false;
        }
    };

    out.reserve((len / 5) * 4);

    for (size_t i = 0; i < len; i += 5)
    {
        uint64_t bits = 0;

        bits |= static_cast<uint64_t>(in[i + 0]) << 32;
        bits |= static_cast<uint64_t>(in[i + 1]) << 24;
        bits |= static_cast<uint64_t>(in[i + 2]) << 16;
        bits |= static_cast<uint64_t>(in[i + 3]) << 8;
        bits |= static_cast<uint64_t>(in[i + 4]);

        uint8_t n[8] = {};

        for (int j = 0; j < 8; ++j)
        {
            const int shift = 35 - (j * 5);
            const uint8_t code = static_cast<uint8_t>((bits >> shift) & 0x1F);

            if (!decode5(code, n[j]))
                return false;
        }

        out.push_back(static_cast<uint8_t>((n[0] << 4) | n[1]));
        out.push_back(static_cast<uint8_t>((n[2] << 4) | n[3]));
        out.push_back(static_cast<uint8_t>((n[4] << 4) | n[5]));
        out.push_back(static_cast<uint8_t>((n[6] << 4) | n[7]));
    }

    return true;
}

int D1571::sectorsPerTrack1541(int track1based)
{
    if (track1based <= 17) return 21;
    if (track1based <= 24) return 19;
    if (track1based <= 30) return 18;
    return 17; // 31..35
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

    // Hard reset in case user switched disk
    reset();

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
    else if (ext == ".d64" || ext == ".g64")
        mediaPath = MediaPath::GCR_D64;
    else
        mediaPath = MediaPath::FDC_MFM;

    #ifdef Debug
    std::cout << "[D1571:LOAD] path=" << path
              << " mediaPath="
              << (mediaPath == MediaPath::GCR_D71 ? "GCR_D71" :
                  mediaPath == MediaPath::GCR_D64 ? "GCR_D64" :
                  "FDC_MFM")
              << "\n";
    #endif

    // Success load it
    diskImage      = std::move(img);
    diskLoaded     = true;
    loadedDiskName = path;
    lastError      = DriveError::NONE;
    gcrDirty       = true;

    invalidateRawGcrCache();
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

void D1571::atnChanged(bool atnLow)
{
    if (atnLow == atnLineLow) return; // ignore no change

    bool prev = atnLineLow;
    atnLineLow = atnLow;

    // Force clk to release when Atn is asserted by the C64
    if (atnLineLow) peripheralAssertClk(false);

    // Keep VIA in sync with the new ATN level (PB4 input)
    auto& via1 = d1571mem.getVIA1();
    via1.setIECInputLines(atnLineLow, clkLineLow, dataLineLow);

    // If ATN just asserted (bus high->low), this is the start of a new command
    // phase. Resync the serial shift register so we don't carry partial bytes.
    if (!prev && atnLineLow)
    {
        via1.resetShift();
    }

    // CA1 polarity fix: treat ATN assert (high->low on bus) as CA1 rising
    bool ca1Rising  = (!prev && atnLineLow);   // ATN high->low
    bool ca1Falling = ( prev && !atnLineLow);  // ATN low->high
    via1.onCA1Edge(ca1Rising, ca1Falling);
}

void D1571::clkChanged(bool clkLow)
{
    if (clkLow == clkLineLow) return; // ignore no change

    bool prevClkLow  = clkLineLow;
    clkLineLow       = clkLow;

    bool prevClkHigh = !prevClkLow;
    bool clkHigh     = !clkLow;

    // Edge detection on the bus CLK line
    bool rising  = (!prevClkHigh && clkHigh);    // low -> high
    bool falling = ( prevClkHigh && !clkHigh );  // high -> low

    // Normal path: just update VIA with the new CLK level
    auto& via1 = d1571mem.getVIA1();
    via1.setIECInputLines(atnLineLow, clkLineLow, dataLineLow);
    via1.onClkEdge(rising, falling);
}

void D1571::dataChanged(bool dataLow)
{
    if (dataLow == dataLineLow) return; // ignore no change

    // Bus DATA line changed (dataLow=true -> line pulled low, false -> high).
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

    #ifdef Debug
    std::cout << "[D1571] onListen() device=" << int(deviceNumber)
              << " listening=1 talking=0\n";
    #endif
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

    #ifdef Debug
    std::cout << "[D1571] onUnListen() device=" << int(deviceNumber) << "\n";
    #endif // Debug
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

    #ifdef Debug
    std::cout << "[D1571] onTalk() device=" << int(deviceNumber)
              << " talking=1 listening=0\n";
    #endif
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

    #ifdef Debug
    std::cout << "[D1571] onUnTalk() device=" << int(deviceNumber) << "\n";
    #endif
}

void D1571::onSecondaryAddress(uint8_t sa)
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

    std::cout << "[D1571] onSecondaryAddress() device=" << int(deviceNumber)
              << " sa=" << int(sa) << meaning << "\n";
    #endif
}

void D1571::onVIA2PortAWrite(uint8_t value, uint8_t ddrA)
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
}

void D1571::setDiskWriteGate(bool enabled)
{
    if (diskWriteGate == enabled)
        return;

    diskWriteGate = enabled;

#ifdef Debug
    std::cout << "[D1571:WRITE-GATE] "
              << (enabled ? "ON" : "OFF")
              << " PC=$"
              << std::hex << std::uppercase << driveCPU.getPC()
              << std::dec
              << " pos=" << gcrPos
              << " T" << int(currentTrack + 1)
              << " S" << int(currentSector)
              << "\n";
#endif

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

size_t D1571::findHeaderPosForSector(uint8_t track, uint8_t sector) const
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

        if (!gcrDecodeBytes(&gcrTrackStream[pos], HEADER_GCR_SIZE, raw))
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

    if (gcrTrackStream.empty())
        return false;

    const size_t headerPos = findHeaderPosForSector(track, sector);
    if (headerPos == SIZE_MAX)
        return false;

    const size_t n = gcrTrackStream.size();

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
            gcrBlock.push_back(gcrTrackStream[(dataStart + i) % n]);

        std::vector<uint8_t> raw;
        raw.reserve(260);

        if (!gcrDecodeBytes(gcrBlock.data(), DATA_GCR_SIZE, raw))
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

    const size_t cacheTrack = currentRawCacheIndex();

    if (cacheTrack >= rawGcrTrackDirty.size())
        return;

    if (!rawGcrTrackDirty[cacheTrack])
        return;

    // Make sure current live raw track is cached first.
    saveCurrentRawTrackToCache();

    const uint8_t trackOnSide1based = currentTrackOnSide1Based();
    const uint8_t imageTrack1based  = currentImageTrack1Based();
    const int spt = sectorsPerTrack1541(trackOnSide1based);

    int written = 0;
    int failed = 0;

    for (int sector = 0; sector < spt; ++sector)
    {
        std::vector<uint8_t> sectorBytes;

        if (!decodeRawSectorFromCurrentTrack(trackOnSide1based, static_cast<uint8_t>(sector), sectorBytes))
        {
            ++failed;
            continue;
        }

        if (diskImage->writeSector(imageTrack1based, static_cast<uint8_t>(sector), sectorBytes))
        {
            ++written;
        }
        else
        {
            ++failed;
        }
    }

    #ifdef Debug
    std::cout << "[D1571:FLUSH-TRACK] T"
              << int(imageTrack1based)
              << " written=" << written
              << " failed=" << failed
              << "\n";
    #endif

    rawGcrTrackDirty[cacheTrack] = false;
}

void D1571::flushAllDirtyRawTracksToImage()
{
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

        gcrTrackStream = rawGcrTrackCache[t];
        gcrSync        = rawGcrSyncCache[t];
        gcrSectorAtPos = rawGcrSectorCache[t];

        if (!gcrTrackStream.empty())
            gcrPos %= gcrTrackStream.size();
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
        gcrTrackStream = rawGcrTrackCache[restoreIndex];
        gcrSync        = rawGcrSyncCache[restoreIndex];
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

    gcrTrackStream.clear();
    gcrSync.clear();
    gcrSectorAtPos.clear();
    gcrWrittenMask.clear();

    gcrPos = 0;
    gcrDirty = true;
}

void D1571::flushAndSaveDisk()
{
    flushAllDirtyRawTracksToImage();

    if (diskImage && !loadedDiskName.empty())
    {
#ifdef Debug
        std::cout << "[D1571:SAVE-DISK] saving "
                  << loadedDiskName
                  << " dirty=" << (diskImage->isDirty() ? 1 : 0)
                  << "\n";
#endif

        diskImage->saveDisk(loadedDiskName);
        diskImage->clearDirty();
    }
}

void D1571::saveCurrentRawTrackToCache()
{
    const size_t t = currentRawCacheIndex();

    if (t >= rawGcrTrackCache.size())
        return;

    rawGcrTrackCache[t]  = gcrTrackStream;
    rawGcrSyncCache[t]   = gcrSync;
    rawGcrSectorCache[t] = gcrSectorAtPos;

    rawGcrTrackValid[t] = !gcrTrackStream.empty();

    if (trackModifiedByWrite)
    {
        rawGcrTrackDirty[t] = true;
        trackModifiedByWrite = false;
    }
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
