// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include <stdexcept>
#include "Serial/TCPSerialEndpoint.h"

TCPSerialEndpoint::TCPSerialEndpoint() :
    address(nullptr),
    socket(nullptr)
{
    if (!NET_Init())
         throw std::runtime_error(std::string("Unable to initialize SDL3_net: ") + SDL_GetError());
}

TCPSerialEndpoint::~TCPSerialEndpoint()
{
    disconnect();
    NET_Quit();
}

void TCPSerialEndpoint::reset()
{
    while (!receiveQueue.empty())
        receiveQueue.pop();

    while (!transmitQueue.empty())
        transmitQueue.pop();
}

void TCPSerialEndpoint::tick()
{

}

bool TCPSerialEndpoint::connect(const std::string& host, uint16_t port)
{
    disconnect();

    address = NET_ResolveHostname(host.c_str());

    if (!address)
        return false;

    const NET_Status resolveStatus = NET_WaitUntilResolved(address, 5000);

    if (resolveStatus != NET_SUCCESS)
    {
        NET_UnrefAddress(address);
        address = nullptr;
        return false;
    }

    NET_StreamSocket* newSocket = NET_CreateClient(address, port, 0);

    NET_UnrefAddress(address);
    address = nullptr;

    if (!newSocket)
        return false;

    const NET_Status connectStatus = NET_WaitUntilConnected(newSocket, 5000);

    if (connectStatus != NET_SUCCESS)
    {
        NET_DestroyStreamSocket(newSocket);
        return false;
    }

    socket = newSocket;

    return true;
}

void TCPSerialEndpoint::disconnect()
{
    if (socket)
    {
        NET_DestroyStreamSocket(socket);
        socket = nullptr;
    }
}

bool TCPSerialEndpoint::isConnected() const
{
    if (!socket)
        return false;

    return NET_GetConnectionStatus(socket) == NET_SUCCESS;
}

bool TCPSerialEndpoint::hasByte() const
{
    return !receiveQueue.empty();
}

bool TCPSerialEndpoint::readByte(uint8_t& value)
{
    if (receiveQueue.empty())
        return false;

    value = receiveQueue.front();
    receiveQueue.pop();

    return true;
}

void TCPSerialEndpoint::writeByte(uint8_t value)
{
    transmitQueue.push(value);
}
