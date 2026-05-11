// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef REU_H
#define REU_H

#include <vector>
#include "Common/REUModel.h"

class REU
{
    public:
        REU();
        virtual ~REU();

        void reset();

        uint8_t readIO(uint16_t addr);
        void writeIO(uint16_t addr, uint8_t value);

    protected:

    private:

        std::vector<uint8_t> ram;

        struct REURegisters
        {
            uint8_t status         = 0x10; // version bits / initial status can be refined later
            uint8_t command        = 0x00;

            uint16_t c64Address    = 0x0000;

            uint16_t reuAddressLo  = 0x0000;
            uint8_t  reuBank       = 0x00;

            uint16_t transferLen   = 0x0000;

            uint8_t irqMask        = 0x00;
            uint8_t addressControl = 0x00;
        };

        REURegisters regs;

        REUModel model;
};

#endif // REU_H
