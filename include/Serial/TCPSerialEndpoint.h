// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef TCPSERIALENDPOINT_H
#define TCPSERIALENDPOINT_H

#include <queue>
#include <SDL3_net/SDL_net.h>
#include <string>
#include "Serial/RS232Endpoint.h"

class TCPSerialEndpoint : public RS232Endpoint
{
    public:
        TCPSerialEndpoint();
        virtual ~TCPSerialEndpoint();

        void reset() override;
        void tick() override;

        bool connect(const std::string& host, uint16_t port);
        void disconnect();

        bool isConnected() const;

        bool hasByte() const override;
        bool readByte(uint8_t& value) override;
        void writeByte(uint8_t value) override;

    private:
        NET_Address* address;
        NET_StreamSocket* socket;

        std::queue<uint8_t> receiveQueue;
        std::queue<uint8_t> transmitQueue;
};

#endif // TCPSERIALENDPOINT_H
