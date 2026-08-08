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

        std::string debugString() const;

    private:
        UserPortDevice* device;
};

#endif // USERPORT_H
