// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef USERPORT_H
#define USERPORT_H

#include <cstdint>
#include <string>
#include "Serial/RS232Device.h"

class UserPortDevice;

class UserPort
{
    public:
        UserPort();
        virtual ~UserPort();

        inline void attachDevice(UserPortDevice* device) { this->device = device; }
        inline void detachDevice() { device = nullptr; }

        void reset();
        void tick(uint32_t cyclesElapsed);

        void portAChanged(uint8_t value, uint8_t ddr);
        void portBChanged(uint8_t value, uint8_t ddr);

        uint8_t readPortB() const;

        bool getFlag2() const;

        std::string debugString() const;
        std::string debugRS232String() const;
        std::string selfTestRS232(uint8_t value, RS232Device::Parity parity);
        std::string selfTestRS232Multi();
        std::string selfTestUserPortRS232Formats();
        std::string selfTestUserPortRS232FlowControl();
        std::string selfTestUserPortRS232Errors();

    private:
        UserPortDevice* device;
};

#endif // USERPORT_H
