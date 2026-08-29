// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef D1541BUS_H
#define D1541BUS_H

#include <cstdint>
#include "CPUBus.h"

class D1541Memory;

class D1541Bus : public CPUBus
{
    public:
        D1541Bus();
        virtual ~D1541Bus();

        inline void attachMemoryInstance(D1541Memory* mem) { this->mem = mem; }

        uint8_t read(uint16_t address) override;
        void write(uint16_t address, uint8_t value) override;
        uint8_t peek(uint16_t address) const override;

    private:
        // Non-owning pointers
        D1541Memory* mem;
};

#endif // D1541BUS_H
