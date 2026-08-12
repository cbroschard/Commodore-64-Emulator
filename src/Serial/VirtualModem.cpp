// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include <algorithm>
#include <cctype>
#include <charconv>
#include "Serial/VirtualModem.h"

VirtualModem::VirtualModem() :
    mode(Mode::Command),
    echoEnabled(false)
{

}

VirtualModem::~VirtualModem() = default;

void VirtualModem::reset()
{
    tcp.reset();

    mode = Mode::Command;
    commandBuffer.clear();

    echoEnabled = false;

    while (!receiveQueue.empty())
        receiveQueue.pop();
}

void VirtualModem::tick()
{
    tcp.tick();

    if (mode == Mode::Online)
    {
        uint8_t value = 0;

        while (tcp.readByte(value))
            receiveQueue.push(value);
    }
}

bool VirtualModem::hasByte() const
{
    return !receiveQueue.empty();
}

bool VirtualModem::readByte(uint8_t& value)
{
    if (receiveQueue.empty())
        return false;

    value = receiveQueue.front();
    receiveQueue.pop();

    return true;
}

void VirtualModem::writeByte(uint8_t value)
{
    if (mode == Mode::Command)
    {
        if (echoEnabled)
            receiveQueue.push(value);

        if (value == '\r')
        {
            processCommand();
            commandBuffer.clear();
        }
        else
            commandBuffer.push_back(static_cast<char>(value));

        return;
    }

    if (mode == Mode::Online)
    {
        tcp.writeByte(value);
    }
}

void VirtualModem::processCommand()
{
    // Keep the original command intact for arguments such as hostnames.
    const std::string originalCommand = commandBuffer;

    // Normalize a copy for case-insensitive Hayes command parsing.
    std::string command = commandBuffer;

    std::transform(command.begin(), command.end(), command.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c));} );

    if (command == "AT")
    {
        sendResponse("OK");
        return;
    }
    else if (command == "ATH" || command == "ATH0")
    {
        tcp.disconnect();
        mode = Mode::Command;

        sendResponse("OK");
        return;
    }
    else if (command == "ATI" || command == "ATI0")
    {
        sendResponse("c64 emulator virtual modem by christopher croschard");
        return;
    }
    else if (command == "ATE0")
    {
        echoEnabled = false;
        sendResponse("OK");
        return;
    }
    else if (command == "ATE1")
    {
        echoEnabled = true;
        sendResponse("OK");
        return;
    }
    else if (command == "ATM0" ||
             command == "ATM1" ||
             command == "ATM2" ||
             command == "ATM3")
    {
        // Speaker commands are accepted but have no effect
        // for the TCP-based virtual modem.
        sendResponse("OK");
        return;
    }
    else if (command == "ATT")
    {
        // Tone dialing. No behavioral difference for TCP.
        sendResponse("OK");
        return;
    }
    else if (command == "ATP")
    {
        // Pulse dialing. No behavioral difference for TCP.
        sendResponse("OK");
        return;
    }
    else if (command.starts_with("ATDT") || command.starts_with("ATDP"))
    {
        // Use the ORIGINAL command here so we don't unnecessarily
        // modify the hostname or dial target.
        const std::string target = originalCommand.substr(4);

        const size_t colonPos = target.rfind(':');

        if (colonPos == std::string::npos)
        {
            sendResponse("ERROR");
            return;
        }

        const std::string host = target.substr(0, colonPos);
        const std::string portText = target.substr(colonPos + 1);

        if (host.empty() || portText.empty())
        {
            sendResponse("ERROR");
            return;
        }

        unsigned int parsedPort = 0;

        const auto [ptr, ec] = std::from_chars(portText.data(), portText.data() + portText.size(), parsedPort);

        if (ec != std::errc{} || ptr != portText.data() + portText.size() || parsedPort == 0 || parsedPort > 65535)
        {
            sendResponse("ERROR");
            return;
        }

        const uint16_t port = static_cast<uint16_t>(parsedPort);

        if (tcp.connect(host, port))
        {
            mode = Mode::Online;
            sendResponse("CONNECT");
        }
        else
        {
            sendResponse("NO CARRIER");
        }

        return;
    }

    sendResponse("ERROR");
}

void VirtualModem::sendResponse(const std::string& text)
{
    receiveQueue.push('\r');
    receiveQueue.push('\n');

    for (char ch : text)
        receiveQueue.push(static_cast<uint8_t>(ch));

    receiveQueue.push('\r');
    receiveQueue.push('\n');
}
