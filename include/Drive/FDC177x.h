// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef FDC177X_H
#define FDC177X_H

#include <cstdint>

class FDC177x
{
    public:
        FDC177x();
        virtual ~FDC177x();

        void reset();
        void tick();

        uint8_t readRegister(uint16_t address);
        void writeRegister(uint16_t address, uint8_t value);

    protected:

    private:
};

#endif // FDC177X_H
