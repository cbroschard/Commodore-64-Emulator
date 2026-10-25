// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef D2031BUS_H
#define D2031BUS_H

#include "CPUBus.h"

class D2031Memory;

class D2031Bus : public CPUBus
{
    public:
        D2031Bus();
        ~D2031Bus() override;

        inline void attachMemoryInstance(D2031Memory* mem) { this->mem = mem; }

        uint8_t read(uint16_t address) override;
        void write(uint16_t address, uint8_t value) override;
        uint8_t peek(uint16_t address) const override;

    private:
        D2031Memory* mem;
};

#endif // D2031BUS_H
