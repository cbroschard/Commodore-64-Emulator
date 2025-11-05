// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef D1571VIA_H
#define D1571VIA_H

#include <cstdint>

class D1571VIA
{
    public:
        D1571VIA();
        virtual ~D1571VIA();

        uint8_t readRegister(uint16_t address);
        void writeRegister(uint16_t address, uint8_t value);

        void reset();

    protected:

    private:

        struct viaRegs
        {
            // Ports and data direction
            uint8_t orbIRB;
            uint8_t oraIRA;
            uint8_t ddrB;
            uint8_t ddrA;

            // Timer 1
            uint8_t timer1CounterLowByte;
            uint8_t timer1CounterHighByte;
            uint8_t timer1LowLatch;
            uint8_t timer1HighLatch;

            // Timer 2
            uint8_t timer2CounterLowByte;
            uint8_t timer2CounterHighByte;

            // Serial Shift, Control, and Interrupts
            uint8_t serialShift;
            uint8_t auxControlRegister;
            uint8_t peripheralControlRegister;
            uint8_t interruptFlag;
            uint8_t interruptEnable;
            uint8_t oraIRANoHandshake;
        } registers;

        uint16_t t1Counter;
        uint16_t t1Latch;
        bool t1Running;
        uint16_t t2Counter;
        uint16_t t2Latch;
};

#endif // D1571VIA_H
