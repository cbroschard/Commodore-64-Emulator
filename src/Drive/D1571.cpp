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
    mediaPath(MediaPath::GCR_1541),
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
    gcrByteReadyLow(false),
    gcrShiftReg(0),
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
    gcrDirty(true)
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

    // Feed VIA2 shift register in 1541/GCR mode
    if (isGCRMode() && motorOn && diskLoaded)
    {
        if (++gcrBitCounter >= 32)
        {
            gcrTick();
            gcrBitCounter = 0;
        }
    }

    d1571Mem.tick();
    driveCPU.tick();
}

void D1571::gcrTick()
{
    if (gcrDirty)
    {
        rebuildGCRTrackStream();
        gcrDirty = false;
        gcrPos = 0;
    }

    // Don’t overwrite a byte the ROM hasn’t consumed yet
    if (gcrByteReadyLow)
        return;

    if (gcrTrackStream.empty())
        return;

    gcrShiftReg = gcrTrackStream[gcrPos++];
    if (gcrPos >= gcrTrackStream.size()) gcrPos = 0;

    gcrByteReadyLow = true;

    // sync detect (very rough start: treat 0xFF as sync byte)
    d1571Mem.getVIA2().diskByteFromMedia(gcrShiftReg, (gcrShiftReg == 0xFF));
}

void D1571::reset()
{
    motorOn = false;
    diskWriteProtected = false;
    lastError = DriveError::NONE;
    status = DriveStatus::IDLE;
    currentTrack = 17;
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
    currentListenSA             = 0;
    currentTalkSA               = 0;
    iecRxActive                 = false;
    iecRxBitCount               = 0;
    iecRxByte                   = 0;

    // 1571 Runtime Properties reset
    gcrByteReadyLow     = false;
    gcrShiftReg         = 0;
    currentSide         = 0;
    busDriversEnabled   = false;
    twoMHzMode          = false;
    halfTrackPos        = currentTrack * 2;
    gcrBitCounter       = 0;
    gcrPos              = 0;
    gcrDirty            = true;

    gcrTrackStream.clear();

    d1571Mem.reset();
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

void D1571::setDensityCode(uint8_t code)
{
    uint8_t oldCode = densityCode;
    if (oldCode != code)
    {
        densityCode     = code & 0x03;
        gcrDirty        = true;
        gcrByteReadyLow = false;
    }
}

void D1571::setHeadSide(bool side1)
{
    bool prevSide = currentSide;
    if(prevSide != side1)
    {
        currentSide     = side1 ? 1 : 0;
        gcrDirty        = true;
        gcrByteReadyLow = false;
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
    if (isGCRMode()) return gcrByteReadyLow;

    auto* fdc = getFDC();
    if (!fdc) return false;

    bool drqActive = fdc->checkDRQActive();
    bool intrqActive = fdc->checkIRQActive();
    return drqActive || intrqActive;
}

uint8_t D1571::gcrReadShiftReg()
{
    gcrByteReadyLow = false; // ROM consumed it
    return gcrShiftReg;
}

void D1571::gcrWriteShiftReg(uint8_t value)
{
    gcrShiftReg = value;
}

void D1571::rebuildGCRTrackStream()
{
    gcrTrackStream.clear();
    gcrPos = 0;

    if (!diskLoaded || !diskImage)
        return;

    const int trackOnSide1based = int(currentTrack) + 1; // 1..35
    const int spt = sectorsPerTrack1541(trackOnSide1based);

    // D71: second side is tracks 36..70 in the image.
    const int imageTrack1based = trackOnSide1based + (currentSide ? 35 : 0);

    // Disk ID bytes (normally from BAM at track 18 sector 0)
    auto bam = diskImage->readSector(18, 0);
    uint8_t id1 = bam[0xA2];
    uint8_t id2 = bam[0xA3];

    auto pushN = [&](uint8_t v, int count) {
        gcrTrackStream.insert(gcrTrackStream.end(), count, v);
    };

    // lead-in gap
    pushN(0x55, 64);

    for (int sector = 0; sector < spt; ++sector)
    {
        // ---- read 256 bytes from image ----
        std::vector<uint8_t> sec = diskImage->readSector(uint8_t(imageTrack1based), uint8_t(sector));
        if (sec.size() != 256) sec.assign(256, 0x00);

        // DEBUG: log directory track contents
        if (imageTrack1based == 18 && currentSide == 0)
        {
            std::cout << "[1571] Track 18 sector " << sector
                      << " linkTS=" << int(sec[0]) << "/" << int(sec[1])
                      << " fileType0=$" << std::hex << int(sec[2])
                      << " fileType1=$" << int(sec[0x20 + 2])
                      << std::dec << "\n";
        }

        // ---- HEADER ----
        pushN(0xFF, 10); // sync

        uint8_t hdr[8];
        hdr[0] = 0x08;
        hdr[2] = uint8_t(trackOnSide1based);
        hdr[3] = uint8_t(sector);
        hdr[4] = id1;
        hdr[5] = id2;
        hdr[6] = 0x0F;
        hdr[7] = 0x0F;
        hdr[1] = uint8_t(hdr[2] ^ hdr[3] ^ hdr[4] ^ hdr[5]);

        gcrEncodeBytes(hdr, 8, gcrTrackStream); // 8 -> 10
        pushN(0x55, 20);

        // ---- DATA ----
        pushN(0xFF, 10); // sync

        std::vector<uint8_t> raw(260);
        raw[0] = 0x07;  // data block ID

        uint8_t csum = 0;

        // Put checksum at raw[1], data at raw[2..257]
        for (int i = 0; i < 256; ++i)
        {
            uint8_t b = sec[i];
            raw[2 + i] = b;
            csum ^= b;
        }

        raw[1]   = csum;   // checksum byte
        raw[258] = 0x00;   // gap bytes
        raw[259] = 0x00;

        gcrEncodeBytes(raw.data(), raw.size(), gcrTrackStream);
        pushN(0x55, 30);
    }

    // trailing gap so the stream never runs dry
    pushN(0x55, 128);
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

void D1571::onStepperPhaseChange(uint8_t oldPhase, uint8_t newPhase)
{
    oldPhase &= 0x03;
    newPhase &= 0x03;
    if (oldPhase == newPhase) return;

    // delta mod-4: 1 = step inward, 3 = step outward, 2 = ignore for now
    const uint8_t delta = (newPhase - oldPhase) & 0x03;

    int step = 0;
    if (delta == 1) step = +1;
    else if (delta == 3) step = -1;
    else return;

    halfTrackPos    = std::clamp(halfTrackPos + step, 0, 34 * 2);
    currentTrack    = static_cast<uint8_t>(halfTrackPos / 2);
    gcrDirty        = true;
    gcrByteReadyLow = false;
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
    gcrDirty       = true;

    currentTrack  = 17;
    currentSector = 0;
    halfTrackPos = currentTrack * 2;

    gcrTrackStream.clear();
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
    currentSector = 0;
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

void D1571::atnChanged(bool atnLow)
{
    if (atnLow == atnLineLow) return; // ignore no change

    bool prev = atnLineLow;
    atnLineLow = atnLow;

    std::cout << "[D1571] atnChanged: atnLow=" << atnLineLow
              << " (prev=" << prev << ")\n";

    // Keep VIA in sync with the new ATN level (PB4 input)
    auto& via1 = d1571Mem.getVIA1();
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

    std::cout << "[D1571] clkChanged atnLow=" << atnLineLow
              << " clkLow=" << clkLow
              << " dataLow=" << dataLineLow
          << " rising=" << rising
              << " falling=" << falling << "\n";

    // Normal path: just update VIA with the new CLK level
    auto& via1 = d1571Mem.getVIA1();
    via1.setIECInputLines(atnLineLow, clkLineLow, dataLineLow);
    via1.onClkEdge(rising, falling);
}

void D1571::dataChanged(bool dataLow)
{
    if (dataLow == dataLineLow) return; // ignore no change

    if (iecListening && !atnLineLow)
    {
        std::cout << "[D1571] (LISTEN PHASE) seeing C64 data bit; clkLow="
                  << clkLineLow << " dataLow=" << dataLineLow << "\n";
    }

    // Bus DATA line changed (dataLow=true -> line pulled low, false -> high).
    dataLineLow = dataLow;

    auto& via1 = d1571Mem.getVIA1();
    via1.setIECInputLines(atnLineLow, clkLineLow, dataLineLow);

    std::cout << "[D1571] dataChanged atnLow=" << atnLineLow << " clkLow=" << clkLineLow << " dataLow=" << dataLineLow << "\n";
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
