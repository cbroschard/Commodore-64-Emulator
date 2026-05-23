// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef DRIVEVIA6522_H
#define DRIVEVIA6522_H

#include <cstdint>
#include "Drive/DriveChips.h"
#include "Peripheral.h"

class DriveVIA6522 : public DriveVIABase
{
    public:
        enum class VIARole
        {
            Unknown,
            VIA1_IECBus,
            VIA2_Mechanics
        };

        explicit DriveVIA6522(VIARole role = VIARole::Unknown);
        virtual ~DriveVIA6522();

        // Pointers
        inline void attachPeripheralInstance(Peripheral* parentPeripheral) { this->parentPeripheral = parentPeripheral; }

        void reset();
        void tick(uint32_t cycles);

        bool checkIRQActive() const override;

        inline VIARole getRole() const { return role_; }

    protected:
        VIARole role_ = VIARole::Unknown;
        Peripheral* parentPeripheral = nullptr;

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

        uint8_t portBPins;
        uint8_t portAPins;

        void triggerInterrupt(uint8_t sourceMask);
        void clearIFR(uint8_t sourceMask);
        void refreshMasterBit();

    private:
};

#endif // DRIVEVIA6522_H
