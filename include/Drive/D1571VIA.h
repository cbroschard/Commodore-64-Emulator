// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef D1571VIA_H
#define D1571VIA_H

// Forward declarations
class D1571;

#include <cstdint>
#include "Peripheral.h"

class D1571VIA
{
    public:
        D1571VIA();
        virtual ~D1571VIA();

        // Allow VIA1 and VIA2 to define their role
        enum class VIARole
        {
            Unknown,
            VIA1_IECBus,
            VIA2_Mechanics
        };

        void attachPeripheralInstance(Peripheral* parentPeripheral, VIARole viaRole);

        // IRQ helper
        bool checkIRQActive() const;

        void reset();
        void tick();

        uint8_t readRegister(uint16_t address);
        void writeRegister(uint16_t address, uint8_t value);

    protected:

    private:

        // Non-owning pointers
        Peripheral* parentPeripheral;

        // Port A Hardware Setting Bits
        enum : uint8_t
        {
            PORTA_TRACK0_SENSOR     = 0,
            PORTA_FSM_DIRECTION     = 1,
            PORTA_RWSIDE_SELECT     = 2,
            PORTA_UNUSED3           = 3,
            PORTA_UNUSED4           = 4,
            PORTA_PHI2_CLKSEL       = 5,
            PORTA_UNUSED6           = 6,
            PORTA_BYTE_READY        = 7
        };

        // Port B IEC Bits
        enum : uint8_t
        {
            IEC_DATA_IN_BIT  = 0,
            IEC_DATA_OUT_BIT = 1,
            IEC_CLK_IN_BIT   = 2,
            IEC_CLK_OUT_BIT  = 3,
            IEC_ATN_ACK_BIT  = 4,
            IEC_DEV_BIT0     = 5, // device address switch bit 0
            IEC_DEV_BIT1     = 6, // device address switch bit 1
            IEC_ATN_IN_BIT   = 7
        };

        // Role
        VIARole viaRole;

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
        bool t2Running;
};

#endif // D1571VIA_H
