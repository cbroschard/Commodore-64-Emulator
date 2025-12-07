// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Drive/D1541.h"

D1541::D1541(int deviceNumber) :
    // Power on defaults
    motorOn(false),
    diskLoaded(false),
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
    currentSector(0)
{
    setDeviceNumber(deviceNumber);

    driveCPU.attachMemoryInstance(&d1541mem);
    driveCPU.attachIRQLineInstance(d1541mem.getIRQLine());
    d1541mem.getVIA1().attachPeripheralInstance(this, D1541VIA::VIARole::VIA1_IECBus);
    d1541mem.getVIA2().attachPeripheralInstance(this, D1541VIA::VIARole::VIA2_Mechanics);
}

D1541::~D1541() = default;


void D1541::reset()
{
    // Mechanics
    motorOn = false;

    // Status
    lastError           = DriveError::NONE;
    status              = DriveStatus::IDLE;

    // Disk
    diskLoaded          = false;
    diskWriteProtected  = false;
    currentTrack        = 17;
    currentSector       = 0;
    halfTrackPos        = currentTrack * 2;
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

    // CHIPS
    d1541mem.reset();
    driveCPU.reset();
}

void D1541::tick(uint32_t cycles)
{
    while(cycles > 0)
    {
        driveCPU.tick();
        uint32_t dc = driveCPU.getElapsedCycles();
        if(dc == 0) dc = 1;

        Drive::tick(dc);
        d1541mem.tick();
        cycles -= dc;
    }
}

bool D1541::initialize(const std::string& loRom, const std::string& hiRom)
{
    if (!d1541mem.initialize(loRom, hiRom))
    {
        return false;
    }

    reset();
    return true;
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

    // Success load it
    diskImage      = std::move(img);
    diskLoaded     = true;
    loadedDiskName = path;
    lastError      = DriveError::NONE;

    currentTrack  = 17;
    currentSector = 0;
    halfTrackPos = currentTrack * 2;
}

void D1541::unloadDisk()
{
    diskImage.reset();  // Reset disk image by assigning a fresh instance
    loadedDiskName.clear();

    diskLoaded      = false;
    currentTrack    = 0;
    currentSector   = 1;
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

void D1541::clkChanged(bool clkState)
{
    if (bus)
    {
        bus->setClkLine(!clkState);
    }
}

void D1541::dataChanged(bool dataState)
{
    if (bus)
    {
        bus->setDataLine(!dataState);
    }
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
