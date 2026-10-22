// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef IEEE488DEVICE_H
#define IEEE488DEVICE_H

#include <cstdint>
#include "IEEE488/IEEE488Bus.h"

class IEEE488Device
{
    public:
        IEEE488Device();
        virtual ~IEEE488Device();

        IEEE488Device(const IEEE488Device&) = delete;
        IEEE488Device& operator=(const IEEE488Device&) = delete;

        virtual void reset();
        virtual void tick(uint64_t cycles) = 0;

        bool attachBus(IEEE488Bus* bus, IEEE488Bus::DeviceId id);
        void detachBus();

        bool isConnected() const;

    protected:
        bool readSignal(IEEE488Bus::Signal signal) const;
        uint8_t readDIO() const;

        bool driveSignal(IEEE488Bus::Signal signal, bool level);
        bool driveDIO(uint8_t value);

        void releaseBusLines();

    private:
        IEEE488Bus* ieee488Bus;
        IEEE488Bus::DeviceId participantId;
};

#endif // IEEE488DEVICE_H
