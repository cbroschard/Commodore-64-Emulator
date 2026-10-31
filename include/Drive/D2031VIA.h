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
#include "IEEE488/IEEE488Bus.h"

class D2031VIA : public DriveVIA6522
{
    public:
        D2031VIA();
        ~D2031VIA() override;

        // State Management
        void saveState(StateWriter& wrtr) const;
        bool loadState(StateReader& rdr);

        // Reset function
        void reset() override;

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
        void clearIEEE488TransientState();
        void clearMechBytePending();
        void synchronizeIEEE488Outputs();
        void resetShift();
        void pulseWriteByteReady();

        void setIEEE488Inputs(const IEEE488Bus::BusState& oldState, const IEEE488Bus::BusState& newState);

        MechanicsInfo getMechanicsInfo() const override;

    protected:
        void onAttachedToPeripheral() override;
        void onPCRChanged(uint8_t oldValue, uint8_t newValue) override;

        uint8_t samplePortAInputs() override;
        uint8_t samplePortBInputs() override;

        void onPortAOutputChanged(uint8_t oldValue, uint8_t newValue) override;
        void onPortBOutputChanged(uint8_t oldValue, uint8_t newValue) override;

        void onDDRAChanged(uint8_t oldDDR, uint8_t newDDR) override;
        void onDDRBChanged(uint8_t oldDDR, uint8_t newDDR) override;

        void onShiftRegisterWritten() override;

        uint8_t sampleShiftRegisterRead() override;

    private:
        // ============================================================
        // VIA #1 - IEEE-488 Interface ($1800-$180F)
        // ============================================================

        // Port A - IEEE-488 Parallel Data Bus
        enum : uint8_t
        {
            IEEE488_DIO1_BIT = 0,  // PA0
            IEEE488_DIO2_BIT = 1,  // PA1
            IEEE488_DIO3_BIT = 2,  // PA2
            IEEE488_DIO4_BIT = 3,  // PA3
            IEEE488_DIO5_BIT = 4,  // PA4
            IEEE488_DIO6_BIT = 5,  // PA5
            IEEE488_DIO7_BIT = 6,  // PA6
            IEEE488_DIO8_BIT = 7   // PA7
        };

        // Port B - IEEE-488 Handshake and Control
        enum : uint8_t
        {
            IEEE488_ATNA_BIT   = 0,  // PB0 - Attention acknowledge
            IEEE488_NRFD_BIT   = 1,  // PB1 - Not ready for data
            IEEE488_NDAC_BIT   = 2,  // PB2 - Not data accepted
            IEEE488_EOI_BIT    = 3,  // PB3 - End or identify
            IEEE488_TR_BIT     = 4,  // PB4 - Talk/Receive control
            IEEE488_HDSEL_BIT  = 5,  // PB5 - Hardware select
            IEEE488_DAV_BIT    = 6,  // PB6 - Data valid
            IEEE488_ATN_BIT    = 7   // PB7 - Inverted ATN input
        };

        // ============================================================
        // VIA #2 - Disk Mechanism ($1C00-$1C0F)
        // ============================================================

        // Port A - Disk Read/Write Data
        enum : uint8_t
        {
            MECH_DATA0 = 0,  // PA0
            MECH_DATA1 = 1,  // PA1
            MECH_DATA2 = 2,  // PA2
            MECH_DATA3 = 3,  // PA3
            MECH_DATA4 = 4,  // PA4
            MECH_DATA5 = 5,  // PA5
            MECH_DATA6 = 6,  // PA6
            MECH_DATA7 = 7   // PA7
        };

        // Port B - Disk Mechanism Control and Status
        enum : uint8_t
        {
            MECH_STEPPER_PHASE0 = 0, // PB0 - Stepper phase 0
            MECH_STEPPER_PHASE1 = 1, // PB1 - Stepper phase 1
            MECH_SPINDLE_MOTOR  = 2, // PB2 - Spindle motor
            MECH_LED            = 3, // PB3 - Activity LED
            MECH_WRITE_PROTECT  = 4, // PB4 - Write protect sensor
            MECH_DENSITY_BIT0   = 5, // PB5 - Density select 0
            MECH_DENSITY_BIT1   = 6, // PB6 - Density select 1
            MECH_SYNC_DETECTED  = 7  // PB7 - SYNC detected
        };

        // Drive Mechanics
        bool    ledOn;
        bool    syncDetected;
        uint8_t mechDataLatch;
        bool    mechBytePending;
        bool    haveLastHeader;

        // Helpers
        void updateIEEE488OutputsFromPortB();
        void recomputeDiskWriteGate();
        void updateIEEE488DeviceAddress();
};

#endif // D2031VIA_H
