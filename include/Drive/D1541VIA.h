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
#include "Drive/DriveChips.h"
#include "Logging.h"

class D1541VIA : public DriveVIABase
{
    public:
        D1541VIA();
        virtual ~D1541VIA();

        // Allow VIA1 and VIA2 to define their role
        enum class VIARole
        {
            Unknown,
            VIA1_IECBus,
            VIA2_Mechanics
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

        bool checkIRQActive() const override;

        // ML Monitor
        inline viaRegsView getRegsView() const override
        {
            return
            {
                registers.orbIRB,
                registers.oraIRA,
                registers.ddrB,
                registers.ddrA,
                registers.timer1CounterLowByte,
                registers.timer1CounterHighByte,
                registers.timer1LowLatch,
                registers.timer1HighLatch,
                registers.timer2CounterLowByte,
                registers.timer2CounterHighByte,
                registers.serialShift,
                registers.auxControlRegister,
                registers.peripheralControlRegister,
                registers.interruptFlag,
                registers.interruptEnable,
                registers.oraIRANoHandshake
            };
        }

        MechanicsInfo getMechanicsInfo() const override;

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

        // Port B Mechanical Bits
        enum : uint8_t
        {
            MECH_STEPPER_PHASE0 = 0, // PB0: stepper phase bit 0 (output)
            MECH_STEPPER_PHASE1 = 1, // PB1: stepper phase bit 1 (output)
            MECH_SPINDLE_MOTOR  = 2, // PB2: spindle motor on/off (output, 1 = on)
            MECH_LED            = 3, // PB3: drive LED (output, 1 = on)
            MECH_WRITE_PROTECT  = 4, // PB4: write-protect sensor (input, 0 = write-protected)
            MECH_DENSITY_BIT0   = 5, // PB5: density select bit 0 (output)
            MECH_DENSITY_BIT1   = 6, // PB6: density select bit 1 (output)
            MECH_SYNC_DETECTED  = 7  // PB7: sync detected (input, 0 = sync seen)
        };

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
