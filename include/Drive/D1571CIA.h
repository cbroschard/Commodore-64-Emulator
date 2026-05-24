// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef D1571CIA_H
#define D1571CIA_H

#include "Drive/DriveCIA.h"

class D1571CIA : public DriveCIA
{
    public:
        D1571CIA();
        ~D1571CIA() override;

        inline void attachPeripheralInstance(Peripheral* parentPeripheral) { this->parentPeripheral = parentPeripheral; }

        void setIECInputs(bool atnLow, bool clkLow, bool dataLow);
        void primeAtnLevel(bool atnLow);

    protected:
        void portAOutputChanged(uint8_t pra, uint8_t ddra) override;
        void portBOutputChanged(uint8_t prb, uint8_t ddrb) override;
        void irqLineChanged(bool active) override;

    private:
        // Non-owning pointers
        Peripheral* parentPeripheral;

        bool iecAtnInLow = false;
        bool iecClkInLow = false;
        bool iecDataInLow = false;
        bool lastAtnLow = false;

        uint8_t makePortBPins() const;
        void updateInputPins();
        void applyIECOutputs();
};

#endif // D1571CIA_H
