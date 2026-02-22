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
#include "Drive/DriveChips.h"
#include "Peripheral.h"
#include "StateReader.h"
#include "StateWriter.h"

class D1571VIA : public DriveVIABase
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

        // State Management
        void saveState(StateWriter& wrtr) const;
        bool loadState(StateReader& rdr);

        void reset();
        void tick(uint32_t cycles);

        void resetShift();

        uint8_t readRegister(uint16_t address);
        void writeRegister(uint16_t address, uint8_t value);

        // Drive mechanics
        inline bool isLedOn() const { return ledOn; }
        inline void setLed(bool on) { ledOn = on; }
        inline bool isSyncDetected() const { return syncDetected; }
        inline bool mechHasBytePending() const { return mechBytePending; }
        inline void setSyncDetected(bool present) { syncDetected = present; }
        void diskByteFromMedia(uint8_t byte, bool inSync);

        // Setters
        void setIECInputLines(bool atnLow, bool clkLow, bool dataLow);

        // Helpers
        bool checkIRQActive() const override;
        void onClkEdge(bool rising, bool falling);
        void onCA1Edge(bool rising, bool falling);
        void clearMechBytePending();

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
        Peripheral* parentPeripheral;

        // Serial shift
        uint8_t  srShiftReg;
        uint8_t  srBitCount;
        bool     srShiftInMode;

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

        // Role
        VIARole viaRole;

        uint8_t portBPins;
        uint8_t portAPins;

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

        // Drive Mechanics
        bool    ledOn;
        bool    syncDetected;
        uint8_t mechDataLatch;
        bool    mechBytePending;

        // Timers
        uint16_t t1Counter;
        uint16_t t1Latch;
        bool     t1Running;
        uint16_t t2Counter;
        uint16_t t2Latch;
        bool     t2Running;

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

        void triggerInterrupt(uint8_t mask);
        void clearIFR(uint8_t mask);
        void refreshMasterBit();

        // Helper
        void updateIECOutputsFromPortB();
};

#endif // D1571VIA_H
