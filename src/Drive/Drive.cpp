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
    waitingForAck(false),
    ackEdgeCountdown(0),
    prevClkLevel(true),
    ackHold(false),
    byteAckHold(false),
    haveListenCommand(false),
    haveSecondary(false)
{

}

Drive::~Drive() = default;

void Drive::atnChanged(bool atnAsserted)
{
    std::cout << "[Drive] atnChanged called, atn=" << atnAsserted
              << " this=" << this << "\n";

    if (atnAsserted)
    {
        // New ATN phase: prepare to receive a command byte
        bitShiftRegister = 0;
        bitCount         = 0;
        expectingListen  = false;
        expectingTalk    = false;

        currentDriveBusState = DriveBusState::AWAITING_COMMAND;

        // Ignore the *first* falling CLK edge after ATN goes low.
        // That edge is the presence ACK handshake, not the first data bit.
        ackEdgeCountdown = 1;
        lastClkHigh      = true;  // assume CLK idle high before the first fall

        // Do NOT touch DATA here; the 1571 handles presence ACK.
    }
    else
    {
        // ATN released: only force a state if we *haven't* already moved
        // into LISTENING/TALKING from parseCommandByte().
        if (currentDriveBusState == DriveBusState::AWAITING_COMMAND)
        {
            if (expectingListen)
                currentDriveBusState = DriveBusState::LISTENING;
            else if (expectingTalk)
                currentDriveBusState = DriveBusState::TALKING;
            else
                currentDriveBusState = DriveBusState::IDLE;
        }

        expectingListen = false;
        expectingTalk   = false;
    }
}

void Drive::dataChanged(bool dataState)
{
     if (currentDriveBusState == DriveBusState::TALKING && waitingForAck)
    {
        // C64 pulls DATA low to ACK the byte we just sent.
        if (!dataState)
        {
            std::cout << "[Drive] received TALK ACK (DATA low)\n";

            waitingForAck = false;
            talkBitPos = -1;

            // Release DATA so the bus can go back high.
            peripheralAssertData(false);
        }
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
        if (!bus)
        return;

    // Handle releasing DATA after our ACK pulses
    if (ackDelay > 0)
    {
        --ackDelay;
        if (ackDelay == 0)
        {
            // Release DATA back to "not driving low"
            peripheralAssertData(false);
        }
    }
}

void Drive::parseCommandByte(uint8_t byte)
{
    std::cout << "[Drive] parseCommandByte $" << std::hex << int(byte) << std::dec
              << " for device " << deviceNumber << "\n";

    uint8_t code = byte & 0xF0;
    uint8_t dev  = byte & 0x0F;

    // --- UNLISTEN (all devices) ---
    if (byte == 0x3F)
    {
        std::cout << "[Drive] UNLISTEN (0x3F)\n";

        if (currentDriveBusState == DriveBusState::LISTENING &&
            !listenBuffer.empty())
        {
            // End of LISTEN string, e.g. "0:$"
            processListenBuffer();
        }

        currentDriveBusState    = DriveBusState::AWAITING_COMMAND;
        expectingListen         = false;
        haveListenCommand       = false;
        haveSecondary           = false;
        currentSecondaryAddress = -1;
        return;
    }

    // --- UNTALK (all devices) ---
    if (byte == 0x5F)
    {
        std::cout << "[Drive] UNTALK (0x5F)\n";

        currentDriveBusState    = DriveBusState::IDLE;
        expectingTalk           = false;
        haveSecondary           = false;
        currentSecondaryAddress = -1;
        waitingForAck           = false;
        talkBitPos              = -1;
        return;
    }

    // --- LISTEN <device> ---
    if (code == 0x20 && dev == deviceNumber)
    {
        std::cout << "[Drive] LISTEN for this device\n";

        expectingListen   = true;
        expectingTalk     = false;
        haveListenCommand = true;
        haveSecondary     = false;

        // We stay in AWAITING_COMMAND while ATN is low; once
        // we see a secondary (0xF0 / 0x60 / 0xE0) we treat
        // subsequent bytes as payload => LISTENING.
        return;
    }

    // --- TALK <device> ---
    if (code == 0x40 && dev == deviceNumber)
    {
        std::cout << "[Drive] TALK for this device\n";

        expectingTalk     = true;
        expectingListen   = false;
        haveListenCommand = false;
        haveSecondary     = false;

        currentDriveBusState = DriveBusState::TALKING;
        return;
    }

    // --- Secondary address / "reopen channel" 0x60–0x6F ---
    if (code == 0x60)
    {
        currentSecondaryAddress = dev;   // channel # (0–15)
        haveSecondary           = true;

        std::cout << "[Drive] Secondary/reopen channel=" << int(dev) << "\n";

        // In LISTEN context, following bytes are command string.
        if (expectingListen)
        {
            currentDriveBusState = DriveBusState::LISTENING;
        }
        return;
    }

    // --- OPEN channel 0xF0–0xFF (we only really care about chan 0) ---
    if (code == 0xF0)
    {
        std::cout << "[Drive] OPEN channel " << int(dev) << "\n";

        currentSecondaryAddress = dev;
        haveSecondary           = true;

        if (expectingListen)
        {
            currentDriveBusState = DriveBusState::LISTENING;
        }
        return;
    }

    // --- CLOSE channel 0xE0–0xEF (can mostly ignore for now) ---
    if (code == 0xE0)
    {
        std::cout << "[Drive] CLOSE channel " << int(dev) << "\n";
        // For now we just clear the SA if it matches.
        if (currentSecondaryAddress == dev)
        {
            currentSecondaryAddress = -1;
            haveSecondary           = false;
        }
        return;
    }

    // Anything else we ignore for now
    std::cout << "[Drive] Unknown/ignored command byte $" << std::hex
              << int(byte) << std::dec << "\n";
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

void Drive::iecClkEdge(bool data, bool clk)
{
    bool rising  = (!lastClkHigh && clk);
    bool falling = ( lastClkHigh && !clk);

    std::cout << "[CLKEDGE] data=" << data
              << " clk=" << clk
              << " rising=" << rising
              << " falling=" << falling
              << " state=" << static_cast<int>(currentDriveBusState)
              << " bitCount=" << int(bitCount)
              << "\n";

    if (currentDriveBusState == DriveBusState::AWAITING_COMMAND &&
        falling && ackEdgeCountdown > 0)
    {
        std::cout << "[Drive] Ignoring ATN handshake falling edge\n";

        --ackEdgeCountdown;
        lastClkHigh = clk;
        return;
    }

    // === 1. C64 -> drive: receive bits while LISTENing or awaiting command ===
    if (currentDriveBusState == DriveBusState::AWAITING_COMMAND ||
        currentDriveBusState == DriveBusState::LISTENING)
    {
        if (falling)
        {
            uint8_t bit = data ? 1 : 0;

            bitShiftRegister = (bitShiftRegister << 1) | bit;
            ++bitCount;

            std::cout << "[Drive] RX bit=" << int(bit)
                      << " bitCount=" << bitCount
                      << " byte=$" << std::hex << int(bitShiftRegister)
                      << std::dec << "\n";

            if (bitCount == 8)
            {
                uint8_t received = bitShiftRegister;

                std::cout << "[Drive] RX full byte $"
                          << std::hex << int(received)
                          << std::dec
                          << " in state=" << static_cast<int>(currentDriveBusState)
                          << "\n";

                if (currentDriveBusState == DriveBusState::AWAITING_COMMAND)
                {
                    // IEC command byte (LISTEN/TALK/secondary addr/etc.)
                    parseCommandByte(received);
                }
                else
                {
                    // Payload data while drive is listening
                    listenBuffer.push_back(received);
                }

                // Byte-level ACK: briefly pull DATA low.
                peripheralAssertData(true);
                ackDelay = 2;   // Drive::tick() will release DATA later

                bitShiftRegister = 0;
                bitCount         = 0;
            }
        }

        lastClkHigh = clk;
        return;
    }

    // === 2. Drive -> C64: TALKING logic (unchanged) ===
    if (currentDriveBusState == DriveBusState::TALKING)
    {
        // While waiting for the C64’s ACK, ignore CLK edges.
        if (waitingForAck)
        {
            lastClkHigh = clk;
            return;
        }

        // Treat C64's falling CLK as "time to output a bit".
        if (falling)
        {
            // If we're not in the middle of a byte, grab the next one.
            if (talkBitPos < 0)
            {
                if (talkQueue.empty())
                {
                    // Nothing left to send: release DATA and go idle.
                    peripheralAssertData(false);
                    currentDriveBusState = DriveBusState::IDLE;
                    lastClkHigh = clk;
                    return;
                }

                currentTalkByte = talkQueue.front();
                talkQueue.pop();
                talkBitPos = 7; // send MSB first
                std::cout << "[Drive] TALK send byte $"
                          << std::hex << int(currentTalkByte) << std::dec << "\n";
            }

            uint8_t bit = (currentTalkByte >> talkBitPos) & 0x01;
            bool pullLow = (bit == 0);  // IEC bus is active-low
            peripheralAssertData(pullLow);

            if (talkBitPos == 0)
            {
                // Finished this byte; release DATA and wait for C64 ACK
                talkBitPos = -1;
                peripheralAssertData(false);
                waitingForAck = true;
                std::cout << "[Drive] TALK finished byte, waiting for ACK\n";
            }
            else
            {
                --talkBitPos;
            }
        }
    }

    lastClkHigh = clk;
}
