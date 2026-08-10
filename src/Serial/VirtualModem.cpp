// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include <charconv>
#include "Serial/VirtualModem.h"

VirtualModem::VirtualModem() :
    mode(Mode::Command)
{

}

VirtualModem::~VirtualModem() = default;

void VirtualModem::reset()
{
    tcp.reset();

    mode = Mode::Command;
    commandBuffer.clear();

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
    if (commandBuffer == "AT")
    {
        sendResponse("OK");
        return;
    }
    else if (commandBuffer == "ATH")
    {
        tcp.disconnect();
        mode = Mode::Command;
        sendResponse("OK");
        return;
    }
    else if (commandBuffer.starts_with("ATDT"))
    {
        const std::string target = commandBuffer.substr(4);
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
            sendResponse("NO CARRIER");

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
