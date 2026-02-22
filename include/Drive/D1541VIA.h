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
class D1541;
class Peripheral;
class IECBUS;

#include <cstdint>
#include "Drive/DriveChips.h"
#include "Drive/GCRCodec.h"
#include "Logging.h"
#include "StateReader.h"
#include "StateWriter.h"

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

        // State Management
        void saveState(StateWriter& wrtr) const;
        bool loadState(StateReader& rdr);

        // Advance VIA via tick
        void tick(uint32_t cycles);

        // Reset all
        void reset();

        void resetShift();

        // Register access
        uint8_t readRegister(uint16_t address);
        void writeRegister(uint16_t address, uint8_t value);

        bool checkIRQActive() const override;

        // Drive mechanics
        inline bool isLedOn() const { return ledOn; }
        inline void setLed(bool on) { ledOn = on; }
        inline bool isSyncDetected() const { return syncDetected; }
        inline bool mechHasBytePending() const { return mechBytePending; }
        inline void setSyncDetected(bool present) { syncDetected = present; }
        void diskByteFromMedia(uint8_t byte, bool inSync);

        // Helpers
        void clearMechBytePending();
        void onClkEdge(bool rising, bool falling);
        void onCA1Edge(bool rising, bool falling);
        void onCA2Edge(bool rising, bool falling);
        void onCB1Edge(bool rising, bool falling);
        void onCB2Edge(bool rising, bool falling);
        void clearIECTransientState();
        void clearMechLatch();

        // Setters
        void setIECInputLines(bool atnLow, bool clkLow, bool dataLow);

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

        // Serial shift
        uint8_t  srShiftReg;
        uint8_t  srBitCount;
        bool     srShiftInMode;

        // IEC Bits
        bool    iecRxPending;
        uint8_t iecRxByte;

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

        // Interrupt Bits
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

        // Handshake
        bool    atnAckArmed;
        bool    atnAckLatch;
        bool    prevAtnAckClear;

        // Timers
        uint16_t t1Counter;
        uint16_t t1Latch;
        bool     t1Running;
        uint16_t t2Counter;
        uint16_t t2Latch;
        bool     t2Running;
        bool     t1JustLoaded;
        bool     t1ReloadPending;
        bool     t1InhibitIRQ;
        bool     t2JustLoaded;
        bool     t2InhibitIRQ;
        uint8_t  t2LowLatchByte;

        // PB7 output when ACR7=1 (timer output)
        bool t1PB7Level = true;

        // Flag to allow priming of the levels on boot
        bool iecInputPrimed;

        // Latched real bus levels (true = line is LOW on the IEC bus)
        bool busAtnLow;
        bool busClkLow;
        bool busDataLow;

        // Shift register counter
        int srCount;

        // CA1 and CA2 pin states
        bool ca1Level;
        bool ca2Level;
        bool cb1Level;
        bool cb2Level;

        void setCA1Level(bool level);
        void setCA2Level(bool level);
        void setCB1Level(bool level);
        void setCB2Level(bool level);

        // IRQ
        void triggerInterrupt(uint8_t mask);
        void clearIFR(uint8_t mask);
        void refreshMasterBit();

                // Helpers
        void updateIECOutputsFromPortB();
        bool isAtnAckClearAsserted() const;
};

#endif // D1541VIA_H
