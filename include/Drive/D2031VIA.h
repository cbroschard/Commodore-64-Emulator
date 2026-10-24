// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef D2031VIA_H
#define D2031VIA_H

#include "Drive/DriveVIA6522.h"

class D2031VIA : public DriveVIA6522
{
    public:
        D2031VIA();
        ~D2031VIA() override;

        void reset() override;

        uint8_t readRegister(uint16_t address);
        void writeRegister(uint16_t address, uint8_t value);

    private:
        void onAttachedToPeripheral() override;
        void onPCRChanged(uint8_t oldValue, uint8_t newValue) override;
};

#endif // D2031VIA_H
