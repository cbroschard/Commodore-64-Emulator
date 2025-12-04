// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef D1541VIA_H
#define D1541VIA_H

// Forward declaration
class Peripheral;
class IECBUS;

#include <cstdint>
#include "Logging.h"

class D1541VIA
{
    public:
        D1541VIA();
        virtual ~D1541VIA();

        // Allow VIA1 and VIA2 to define their role
        enum class VIARole
        {
            Unknown,
            VIA1_DataHandler,
            VIA2_AtnMonitor
        };

        // Pointers
        void attachPeripheralInstance(Peripheral* parentPeripheral, VIARole role);

        // Advance VIA via tick
        void tick(uint32_t cycles);

        // Reset all
        void reset();

        // Register access
        uint8_t readRegister(uint16_t address);
        void writeRegister(uint16_t address, uint8_t value);

    protected:

    private:

        // Non-owning pointers
        Peripheral* parentPeripheral = nullptr;

        VIARole viaRole;

        // Interrupt Handling
        enum : uint8_t
        {
            IFR_CA2    = 0x01, // Bit 0
            IFR_CA1    = 0x02, // Bit 1
            IFR_SR     = 0x04, // Bit 2
            IFR_CB2    = 0x08, // Bit 3
            IFR_CB1    = 0x10, // Bit 4
            IFR_TIMER2 = 0x20, // Bit 5
            IFR_TIMER1 = 0x40, // Bit 6
            IFR_IRQ    = 0x80  // Bit 7: Master Interrupt Flag
        };

        // I/O registers
        struct viaRegs
        {
            uint8_t portA;
            uint8_t portB;
            uint8_t ddrA;
            uint8_t ddrB;
            uint8_t timer1CounterLow;
            uint8_t timer1CounterHigh;
            uint8_t timer1LatchLow;
            uint8_t timer1LatchHigh;
            uint8_t timer2CounterLow;
            uint8_t timer2CounterHigh;
            uint8_t shiftRegister;
            uint8_t auxillaryControlRegister;
            uint8_t peripheralControlRegister;
            uint8_t interruptFlag;
            uint8_t interruptEnable;
        } registers;

        // Timers
        uint16_t t1Counter;
        uint16_t t1Latch;
        bool     t1Running;
        uint16_t t2Counter;
        uint16_t t2Latch;
        bool     t2Running;

        // Shift register counter
        int srCount;

        // IRQ
        void triggerInterrupt(uint8_t mask);
        void clearIFR(uint8_t mask);
        void refreshMasterBit();
};

#endif // D1541VIA_H
