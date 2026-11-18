// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef D4040BUS_H
#define D4040BUS_H

#include <cstdint>
#include "CPUBus.h"
#include "Drive/D4040Memory.h"
#include "IEEE488/IEEE488Device.h"

// Forward declarations
class D4040;

class D4040Bus : public CPUBus
{
    public:
        enum class Processor
        {
            DOS6502,
            FDC6504
        };

        D4040Bus(Processor processor);
        ~D4040Bus();

        inline void attachDriveInstance(D4040* drive) { this->drive = drive; }
        inline void attachMemoryInstance(D4040Memory* mem) { this->mem = mem; }
        inline void attachIEEE488DeviceInstance(IEEE488Device* ieee488Device) { this->ieee488Device = ieee488Device; }

        uint8_t read(uint16_t address) override;
        void write(uint16_t address, uint8_t value) override;

        uint8_t peek(uint16_t address) const override;

        void updateRIOTInputs();
        void updateRIOTInputs(const IEEE488Bus::BusState& state);
        void updateIEEE488Outputs();

    private:
        D4040* drive;
        D4040Memory* mem;
        IEEE488Device* ieee488Device;

        Processor processor;
};

#endif // D4040BUS_H
