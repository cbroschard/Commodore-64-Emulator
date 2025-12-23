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
    gcrDirty(true)
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

    gcrTrackStream.clear();
    gcrSync.clear();

    d1541mem.reset();
    driveCPU.reset();
    d1541mem.getVIA1().setIECInputLines(false, false, false);
}

void D1541::tick(uint32_t cycles)
{
    int32_t remaining = cycles;

    while(remaining > 0)
    {
        driveCPU.tick();
        uint32_t dc = driveCPU.getElapsedCycles();
        if(dc == 0) dc = 1;

        d1541mem.tick(dc);

        if (motorOn && diskLoaded)
            gcrAdvance(dc);

        remaining -= dc;
    }
}

bool D1541::gcrTick()
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

    uint8_t gcrByte = gcrTrackStream[gcrPos];
    bool syncHigh    = (gcrSync[gcrPos] != 0);

    gcrPos = (gcrPos + 1) % gcrTrackStream.size();
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

    if (!diskLoaded || !diskImage) return;

    const int track1based = int(currentTrack) + 1;
    const int spt = gcrCodec.sectorsPerTrack1541(track1based);

    // Pull disk ID from BAM (track 18 sector 0), same as your D1571 code path
    auto bam = diskImage->readSector(18, 0);
    if (bam.size() < 256) bam.resize(256, 0x00);
    const uint8_t id1 = bam[0xA2];
    const uint8_t id2 = bam[0xA3];

    auto pushN = [&](uint8_t v, int count, bool isSync)
    {
        gcrTrackStream.insert(gcrTrackStream.end(), count, v);
        gcrSync.insert(gcrSync.end(), count, isSync ? 1 : 0);
    };

    auto pushEncoded = [&](const uint8_t* in, size_t len)
    {
        // Must be multiple of 4 bytes (hdr=8, raw=260 are OK)
        for (size_t i = 0; i < len; i += 4)
        {
            uint8_t g[5];
            gcrCodec.encode4Bytes(&in[i], g);
            gcrTrackStream.insert(gcrTrackStream.end(), g, g + 5);
            gcrSync.insert(gcrSync.end(), 5, 0);
        }
    };

    // Same “DOS-ish defaults” as D1571
    constexpr int SYNC_LEN   = 10;
    constexpr int HEADER_GAP = 9;
    constexpr int TAIL_GAP   = 9;

    // Lead-in gap (NOT sync)
    pushN(0x55, 64, false);

    for (int sector = 0; sector < spt; ++sector)
    {
        std::vector<uint8_t> sec = diskImage->readSector(uint8_t(track1based), uint8_t(sector));
        if (sec.size() != 256) sec.assign(256, 0x00);

        // ---- HEADER ----
        pushN(0xFF, SYNC_LEN, true);

        uint8_t hdr[8] = {0};
        hdr[0] = 0x08;
        hdr[2] = uint8_t(sector);     // sector
        hdr[3] = uint8_t(track1based);// track
        hdr[4] = id2;
        hdr[5] = id1;
        hdr[6] = 0x0F;
        hdr[7] = 0x0F;
        hdr[1] = uint8_t(hdr[2] ^ hdr[3] ^ hdr[4] ^ hdr[5]); // header checksum

        pushEncoded(hdr, 8);
        pushN(0x55, HEADER_GAP, false);

        // ---- DATA ----
        pushN(0xFF, SYNC_LEN, true);

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

        pushEncoded(raw.data(), raw.size());
        pushN(0x55, TAIL_GAP, false);
    }

    // Trailing gap
    pushN(0x55, 128, false);

    // Sanity
    if (gcrSync.size() != gcrTrackStream.size())
        gcrSync.assign(gcrTrackStream.size(), 0);

    gcrPos = 0;

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

    // Hard reset first to clear any old disks
    reset();

    // Success load it
    diskImage      = std::move(img);
    diskLoaded     = true;
    loadedDiskName = path;
    lastError      = DriveError::NONE;
    gcrDirty       = true;

    currentTrack  = 17;
    currentSector = 0;
    halfTrackPos = currentTrack * 2;

    gcrTrackStream.clear();
}

void D1541::unloadDisk()
{
    diskImage.reset();  // Reset disk image by assigning a fresh instance
    loadedDiskName.clear();

    diskLoaded      = false;
    currentTrack    = 17;
    currentSector   = 0;
    lastError       = DriveError::NONE;
    status          = DriveStatus::IDLE;
}

void D1541::onListen()
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
    std::cout << "[D1541] onListen() device=" << int(deviceNumber)
              << " listening=1 talking=0\n";
    #endif
}

void D1541::onUnListen()
{
    iecListening = false;
    listening    = false;
    iecRxActive = false;
    iecRxBitCount = 0;
    iecRxByte = 0;

    expectingSecAddr  = false;
    expectingDataByte = false;

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

    // After TALK, the next byte from the C64 is a secondary address
    expectingSecAddr        = true;
    expectingDataByte       = false;
    currentSecondaryAddress = 0xFF;

    peripheralAssertClk(false);

    #ifdef Debug
    std::cout << "[D1541] onTalk() device=" << int(deviceNumber)
              << " talking=1 listening=0\n";
    #endif
}

void D1541::onUnTalk()
{
    iecTalking = false;
    talking    = false;
    iecRxActive = false;
    iecRxBitCount = 0;
    iecRxByte = 0;

    expectingSecAddr  = false;
    expectingDataByte = false;

    #ifdef Debug
    std::cout << "[D1541] onUnTalk() device=" << int(deviceNumber) << "\n";
    #endif
}

void D1541::onSecondaryAddress(uint8_t sa)
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

    #ifdef Debug
    std::cout << "[D1541] onSecondaryAddress() device=" << int(deviceNumber)
              << " sa=" << int(sa) << meaning << "\n";
    #endif
}

void D1541::atnChanged(bool atnLow)
{
    if (atnLow == atnLineLow) return;

    bool prevAtnLow = atnLineLow;
    atnLineLow = atnLow;

    auto& via1 = d1541mem.getVIA1();
    via1.setIECInputLines(atnLineLow, clkLineLow, dataLineLow);

    if (!prevAtnLow && atnLineLow)
        via1.resetShift();
}

void D1541::clkChanged(bool clkLow)
{
    if (clkLow == clkLineLow)
        return; // ignore no change

    clkLineLow = clkLow;

    auto& via1 = d1541mem.getVIA1();
    via1.setIECInputLines(atnLineLow, clkLineLow, dataLineLow);
}

void D1541::dataChanged(bool dataLow)
{
    if (dataLow == dataLineLow)
        return; // ignore no change

    // Bus DATA line changed (dataLow=true -> line pulled low, false -> high).
    dataLineLow = dataLow;

    auto& via1 = d1541mem.getVIA1();
    via1.setIECInputLines(atnLineLow, clkLineLow, dataLineLow);
}

void D1541::setDensityCode(uint8_t code)
{
    code &= 0x03;
    if (densityCode != code) densityCode = code;
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

    halfTrackPos = std::clamp(halfTrackPos + step, 0, 34 * 2);  // 0..68 halftracks
    currentTrack = uint8_t(halfTrackPos / 2);                   // 0..34 (=> track 1..35)
    gcrDirty = true;
}

int D1541::cyclesPerByteFromDensity(uint8_t code) const
{
    static constexpr int kCycles[4] = { 32, 30, 28, 26 };

    return kCycles[code & 0x03];
}

Drive::IECSnapshot D1541::snapshotIEC() const
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
