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

class D4040Bus : public CPUBus
{
    public:
        D4040Bus();
        virtual ~D4040Bus();

        uint8_t read(uint16_t address) override;
        void write(uint16_t address, uint8_t value) override;

        uint8_t peek(uint16_t address) const override;

    private:
        D4040Memory* mem;
};

#endif // D4040BUS_H
