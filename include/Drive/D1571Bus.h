// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author
#ifndef D1571BUS_H
#define D1571BUS_H

#include <cstdint>
#include "CPUBus.h"

class D1571Memory;

class D1571Bus : public CPUBus
{
    public:
        D1571Bus();
        virtual ~D1571Bus();

        inline void attachMemoryInstance(D1571Memory* mem) { this->mem = mem; }

        uint8_t read(uint16_t address) override;
        void write(uint16_t address, uint8_t value) override;
        uint8_t peek(uint16_t address) const override;

    private:
        D1571Memory* mem;
    };

#endif // D1571BUS_H
