// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.

#include "Drive/Drive.h"
#include <iostream>

Drive::Drive() :
    currentDriveError(DriveError::NONE),
    currentDriveStatus(DriveStatus::IDLE),
    currentDriveBusState(DriveBusState::IDLE),
    diskImage(),
    logger(nullptr),
    currentSecondaryAddress(-1),
    waitingForAck(false),
    ackEdgeCountdown(0),
    waitingForClkRelease(false),
    prevClkLevel(true),
    ackHold(false),
    byteAckHold(false),
    ackDelay(0),
    lastClkHigh(true)
{

}

Drive::~Drive() = default;

void Drive::atnChanged(bool atnAsserted)
{
    // atnAsserted == true  → ATN line is low/active.
    std::cout << "[Drive] atnChanged: " << (atnAsserted ? "LOW" : "HIGH")
              << " this=" << this << "\n";

    if (atnAsserted)
    {
        // Enter a generic "command phase". The drive ROM will decode
        // what actually happens while ATN is low.
        currentDriveBusState = DriveBusState::AWAITING_COMMAND;

        // Clear old bit/handshake bookkeeping.
        waitingForAck        = false;
        waitingForClkRelease = false;
        byteAckHold          = false;
        ackEdgeCountdown     = 0;
        lastClkHigh          = true;
    }
    else
    {
        // Leaving ATN. ROM is responsible for deciding whether this
        // resulted in LISTEN, TALK, etc.
        if (currentDriveBusState == DriveBusState::AWAITING_COMMAND)
        {
            currentDriveBusState = DriveBusState::IDLE;
        }

        waitingForAck        = false;
        waitingForClkRelease = false;
        byteAckHold          = false;
        ackEdgeCountdown     = 0;
    }
}

void Drive::dataChanged(bool level)
{
    std::cout << "[Drive] dataChanged (bus notification): DATA="
              << (level ? "HIGH" : "LOW") << "\n";
}

void Drive::driveControlClkLine(bool clkLow)
{
    peripheralAssertClk(clkLow);

     // Debug print
    std::cout << "[Drive] driveControlClkLine: clkLow=" << (clkLow ? 1 : 0)
              << " (dev=" << deviceNumber << ")\n";
}

void Drive::driveControlDataLine(bool dataLow)
{
    peripheralAssertData(dataLow);

     // Debug print
    std::cout << "[Drive] driveControlDataLine: dataLow=" << (dataLow ? 1 : 0)
              << " (dev=" << deviceNumber << ")\n";
}

bool Drive::insert(const std::string &path)
{
    // Detect disk format
    auto fmt = DiskFactory::detectFormat(path);

    // Ask the concrete drive (1571, etc.) if it supports this format
    if (!canMount(fmt))
    {
        currentDriveError  = DriveError::NO_DISK;
        currentDriveStatus = DriveStatus::IDLE;
        std::cerr << "Drive: format not supported for '" << path << "'\n";
        return false;
    }

    // Create disk implementation
    std::unique_ptr<Disk> d = DiskFactory::create(path);
    if (!d)
    {
        currentDriveError  = DriveError::NO_DISK;
        currentDriveStatus = DriveStatus::IDLE;
        std::cerr << "Drive: no disk implementation for '" << path << "'\n";
        return false;
    }

    // Load backing file
    if (!d->loadDisk(path))
    {
        currentDriveError  = DriveError::READ_ERROR;
        currentDriveStatus = DriveStatus::IDLE;
        std::cerr << "Drive: failed to load disk image '" << path << "'\n";
        return false;
    }

    // Take ownership
    diskImage = std::move(d);

    // Let concrete drive sync its internal state (track, side, etc.)
    try
    {
        loadDisk(path);
    }
    catch (...)
    {
        currentDriveError  = DriveError::READ_ERROR;
        currentDriveStatus = DriveStatus::IDLE;
        std::cerr << "Drive: exception while mounting disk '" << path << "'\n";
        return false;
    }

    currentDriveError  = DriveError::NONE;
    currentDriveStatus = DriveStatus::READY;
    return true;
}

void Drive::tick()
{
    if (ackDelay > 0)
    {
        --ackDelay;
        if (ackDelay == 0)
        {
            // Release DATA back to not-driving state if some legacy code used it.
            peripheralAssertData(false);
            byteAckHold = false;
        }
    }
}

Drive::IECSnapshot Drive::snapshotIEC() const
{
    IECSnapshot s{};

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
