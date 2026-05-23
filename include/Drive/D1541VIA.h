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
#include "Drive/DriveVIA6522.h"

class D1541VIA : public DriveVIA6522
{
    public:
        D1541VIA();
        virtual ~D1541VIA();

        // State Management
        void saveState(StateWriter& wrtr) const;
        bool loadState(StateReader& rdr);

        // Reset all
        void reset() override;

        void resetShift();

        // Register access
        uint8_t readRegister(uint16_t address);
        void writeRegister(uint16_t address, uint8_t value);

        // Drive mechanics
        inline bool isLedOn() const { return ledOn; }
        inline void setLed(bool on) { ledOn = on; }
        inline bool isSyncDetected() const { return syncDetected; }
        inline bool mechHasBytePending() const { return mechBytePending; }
        inline void setSyncDetected(bool present) { syncDetected = present; }
        void diskByteFromMedia(uint8_t byte, bool inSync);

        // Helpers
        void clearMechBytePending();
        void pulseWriteByteReady();
        void onClkEdge(bool rising, bool falling);
        void onCA1Edge(bool rising, bool falling);
        void onCA2Edge(bool rising, bool falling);
        void onCB1Edge(bool rising, bool falling);
        void onCB2Edge(bool rising, bool falling);
        void clearIECTransientState();
        void clearMechLatch();

        // Setters
        void setIECInputLines(bool atnLow, bool clkLow, bool dataLow);

        MechanicsInfo getMechanicsInfo() const override;

    protected:

    private:
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

        // Drive Mechanics
        bool    ledOn;
        bool    syncDetected;
        uint8_t mechDataLatch;
        bool    mechBytePending;

        // Handshake
        bool    atnAckArmed;
        bool    atnAckLatch;
        bool    prevAtnAckClear;

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

        // Helpers
        void updateIECOutputsFromPortB();
        void recomputeDiskWriteGate();
        bool isAtnAckClearAsserted() const;
};

#endif // D1541VIA_H
