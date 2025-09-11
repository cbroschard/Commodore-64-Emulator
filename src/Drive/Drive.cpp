// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Drive/Drive.h"

Drive::Drive() :
    currentDriveError(DriveError::NONE),
    currentDriveStatus(DriveStatus::IDLE),
    currentDriveBusState(DriveBusState::IDLE),
    ackDelay(0),
    expectingListen(false),
    expectingTalk(false),
    bitShiftRegister(0),
    bitCount(0),
    lastClkHigh(true),
    currentSecondaryAddress(-1),
    currentTalkByte(0),
    talkBitPos(-1),
    waitingForAck(false)
{

}

Drive::~Drive() = default;

void Drive::attachLoggingInstance(Logging* logger)
{
    this->logger = logger;
}

void Drive::atnChanged(bool atnAsserted)
{
    if (atnAsserted)
    {
        // ATN LOW (Command Mode)
        if (currentDriveBusState == DriveBusState::TALKING)
        {
            peripheralAssertClk(true);   // release CLK
            peripheralAssertData(true); // release DATA
            talkBitPos = -1;
            waitingForAck = false;
            while (!talkQueue.empty()) talkQueue.pop();
        }

        // We're now expecting a command byte
        currentDriveBusState = DriveBusState::AWAITING_COMMAND;
        expectingListen = false;
        expectingTalk = false;
    }
    else
    {
        // ATN HIGH (end of command phase)
        if (expectingListen)
        {
            currentDriveBusState = DriveBusState::LISTENING;
            // You’ll later set up receiver to handle incoming data
        }
        else if (expectingTalk)
        {
            currentDriveBusState = DriveBusState::TALKING;
            // You’ll later implement byte-sending behavior
        }
        else
        {
            currentDriveBusState = DriveBusState::IDLE;
        }

        // Reset expectation flags
        expectingListen = false;
        expectingTalk = false;
    }
}

bool Drive::insert(const std::string& path)
{
    // Figure out what kind of image it is
    auto fmt = DiskFactory::detectFormat(path);

    // Ask *this* drive if it supports that format
    if (!canMount(fmt)) {
        currentDriveError = DriveError::NO_DISK;
        std::cerr << "Drive: format not supported by this drive\n";
        return false;
    }

    // Use the factory to build the right Disk subclass
    std::unique_ptr<Disk> d = DiskFactory::create(path);
    if (!d) {
        currentDriveError = DriveError::NO_DISK;
        std::cerr << "Drive: no disk implementation for format\n";
        return false;
    }

    // take ownership of the loaded Disk
    diskImage = std::move(d);

    // Load the image
    if (!d->loadDisk(path)) {
        currentDriveError = DriveError::READ_ERROR;
        std::cerr << "Drive: failed to load disk image\n";
        return false;
    }

    // Success—keep the loaded disk
    try
    {
        loadDisk(path);
    }
    catch (...)
    {
       currentDriveError = DriveError::READ_ERROR;
       return false;
    }
    currentDriveStatus = DriveStatus::READY;
    return true;
}

void Drive::tick()
{
    if (!bus) return;

    bool clk = bus->readClkLine();
    bool data = bus->readDataLine();

    // === LISTENING or AWAITING_COMMAND: Receiving bits from C64 ===
    if (currentDriveBusState == DriveBusState::AWAITING_COMMAND ||
        currentDriveBusState == DriveBusState::LISTENING)
    {
        if (lastClkHigh && !clk)
        {
            bitShiftRegister = (bitShiftRegister << 1) | (data ? 1 : 0);
            bitCount++;

            if (bitCount == 8)
            {
                if (currentDriveBusState == DriveBusState::AWAITING_COMMAND)
                {
                    parseCommandByte(bitShiftRegister);
                }
                else if (currentDriveBusState == DriveBusState::LISTENING)
                {
                    listenBuffer.push_back(bitShiftRegister);

                    // ACK with DATA low
                    peripheralAssertData(false);
                    ackDelay = 2;

                    // Optional: process buffer on carriage return
                    if (bitShiftRegister == 0x0D)
                        processListenBuffer();
                }

                bitCount = 0;
                bitShiftRegister = 0;
            }
        }
    }
    // === TALKING: Sending bits to C64 ===
    else if (currentDriveBusState == DriveBusState::TALKING && !talkQueue.empty())
    {
        if (!waitingForAck)
        {
            if (talkBitPos == -1)
            {
                currentTalkByte = talkQueue.front();
                talkQueue.pop();
                talkBitPos = 7;
            }

            if (lastClkHigh && !clk)
            {
                bool bit = (currentTalkByte >> talkBitPos) & 1;
                peripheralAssertData(!bit); // Active LOW

                talkBitPos--;

                if (talkBitPos < 0)
                    waitingForAck = true;
            }
        }
        else
        {
            // Wait for C64 to ACK (DATA low)
            if (!data)
            {
                waitingForAck = false;
                talkBitPos = -1;
                peripheralAssertData(true); // release DATA
            }
        }
    }

    // === Handle releasing DATA after ACK in LISTENING ===
    if (ackDelay > 0)
    {
        ackDelay--;
        if (ackDelay == 0)
        {
            peripheralAssertData(true); // release DATA
        }
    }

    lastClkHigh = clk;
}

void Drive::parseCommandByte(uint8_t byte)
{
    uint8_t listenBase = 0x20;
    uint8_t talkBase   = 0x40;

    if ((byte & 0xF0) == listenBase && (byte & 0x0F) == deviceNumber)
    {
        expectingListen = true;
        // std::cout << "LISTEN command received by device " << deviceNumber << "\n";
    }
    else if ((byte & 0xF0) == talkBase && (byte & 0x0F) == deviceNumber)
    {
        expectingTalk = true;
        // std::cout << "TALK command received by device " << deviceNumber << "\n";
    }
    else if ((byte & 0xF0) == 0xE0)
    {
        // Secondary address (after LISTEN or TALK)
        currentSecondaryAddress = byte & 0x0F;
        // std::cout << "Secondary address: " << (int)currentSecondaryAddress << "\n";
    }
    else
    {
        // Unknown or not addressed to this device
    }
}

void Drive::processListenBuffer()
{
    std::string command(listenBuffer.begin(), listenBuffer.end());

    std::cout << "[Drive] Received LISTEN string: \"" << command << "\"\n";

    if (command == "0:$")
    {
        std::string fakeDir = "0 \"MYDISK\" 00 2A\n10  \"HELLO\"  PRG\nBLOCKS FREE.\n";
        for (char c : fakeDir)
            talkQueue.push((uint8_t)c);
        talkQueue.push(0x0D); // End with CR
    }

    listenBuffer.clear();
}
