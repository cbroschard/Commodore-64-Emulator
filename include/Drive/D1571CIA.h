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

// Forward declarations
class D1571;

class D1571CIA : public DriveCIA
{
    public:
        D1571CIA();
        ~D1571CIA() override;

        inline void attachPeripheralInstance(Peripheral* parentPeripheral) { this->parentPeripheral = parentPeripheral; }

        void reset() override;

        void setIECInputs(bool atnLow, bool clkLow, bool dataLow);
        void primeAtnLevel(bool atnLow);

        // ML Monitor
        ciaIECDecodeView getIECDecodeView() const override;

    protected:
        void portAOutputChanged(uint8_t pra, uint8_t ddra) override;
        void portBOutputChanged(uint8_t prb, uint8_t ddrb) override;
        void irqLineChanged(bool active) override;

    private:
        // Non-owning pointers
        Peripheral* parentPeripheral;

        enum CIA_PRB : uint8_t
        {
            PRB_DATAIN = 1u << 0,
            PRB_DATOUT = 1u << 1,
            PRB_CLKIN  = 1u << 2,
            PRB_CLKOUT = 1u << 3,
            PRB_ATNACK = 1u << 4,
            PRB_BUSDIR = 1u << 5,
            PRB_WRTPRO = 1u << 6,
            PRB_ATNIN  = 1u << 7
        };

        bool iecAtnInLow = false;
        bool iecClkInLow = false;
        bool iecDataInLow = false;
        bool lastAtnLow = false;

        uint8_t makePortBPins() const;
        void updateInputPins();
        void applyIECOutputs();

        D1571* drive() const;
};

#endif // D1571CIA_H
