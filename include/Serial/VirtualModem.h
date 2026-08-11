// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef VIRTUALMODEM_H
#define VIRTUALMODEM_H

#include "Serial/RS232Endpoint.h"
#include "Serial/TCPSerialEndpoint.h"

class VirtualModem : public RS232Endpoint
{
    public:
        VirtualModem();
        virtual ~VirtualModem();

        void reset() override;
        void tick() override;

        bool hasByte() const override;
        bool readByte(uint8_t& value) override;
        void writeByte(uint8_t value) override;

        bool isOnline() const { return mode == Mode::Online; }

    private:
        enum class Mode
        {
            Command,
            Online
        };

        TCPSerialEndpoint tcp;

        Mode mode;

        bool echoEnabled;

        std::string commandBuffer;
        std::queue<uint8_t> receiveQueue;

        void processCommand();
        void sendResponse(const std::string& text);
};

#endif // VIRTUALMODEM_H
