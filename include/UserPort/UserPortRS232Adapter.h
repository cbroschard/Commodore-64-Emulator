// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef USERPORTRS232ADAPTER_H
#define USERPORTRS232ADAPTER_H

#include "UserPort/UserPortDevice.h"

class RS232Device;

class UserPortRS232Adapter : public UserPortDevice
{
    public:
        UserPortRS232Adapter();
        virtual ~UserPortRS232Adapter();

        void attachRS232DeviceInstance(RS232Device* rs232Device) { this->rs232Device = rs232Device; }

        void reset() override;
        void tick(uint32_t cyclesElapsed) override;

        void portAChanged(uint8_t value, uint8_t ddr) override;
        void portBChanged(uint8_t value, uint8_t ddr) override;

        uint8_t readPortB() const override;

        std::string debugString() const override;

    private:
        RS232Device* rs232Device;

        static constexpr uint8_t TXD_MASK = 0x04; // PA2

        static constexpr uint8_t RXD_MASK = 0x01; // PB0
        static constexpr uint8_t RTS_MASK = 0x02; // PB1
        static constexpr uint8_t DTR_MASK = 0x04; // PB2
        static constexpr uint8_t RI_MASK  = 0x08; // PB3
        static constexpr uint8_t DCD_MASK = 0x10; // PB4
        static constexpr uint8_t CTS_MASK = 0x40; // PB6
        static constexpr uint8_t DSR_MASK = 0x80; // PB7

        uint8_t portAValue = 0xFF;
        uint8_t portADDR   = 0x00;

        uint8_t portBValue = 0xFF;
        uint8_t portBDDR   = 0x00;
};

#endif // USERPORTRS232ADAPTER_H
