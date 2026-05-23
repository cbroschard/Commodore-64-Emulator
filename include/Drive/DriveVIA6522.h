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
#include "StateReader.h"
#include "StateWriter.h"

class DriveVIA6522 : public DriveVIABase
{
    public:
        enum class VIARole
        {
            Unknown,
            VIA1_IECBus,
            VIA2_Mechanics
        };

        DriveVIA6522();
        ~DriveVIA6522() override;

        // Pointers
        void attachPeripheralInstance(Peripheral* parentPeripheral, VIARole role);

        virtual void reset();
        virtual void tick(uint32_t cycles);

        bool checkIRQActive() const override;

        inline VIARole getRole() const { return viaRole; }

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

    protected:
        VIARole viaRole = VIARole::Unknown;
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

        virtual void onAttachedToPeripheral() {}

        void saveVIAState(StateWriter& wrtr) const;
        bool loadVIAState(StateReader& rdr);

        void triggerInterrupt(uint8_t sourceMask);
        void clearIFR(uint8_t sourceMask);
        void refreshMasterBit();

        bool readTimerRegister(uint16_t address, uint8_t& value);
        bool writeTimerRegister(uint16_t address, uint8_t value);

        bool readInterruptRegister(uint16_t address, uint8_t& value);
        bool writeInterruptRegister(uint16_t address, uint8_t value);

        bool readControlRegister(uint16_t address, uint8_t& value);
        bool writeControlRegister(uint16_t address, uint8_t value);

    private:
        enum : uint8_t
        {
            ACR_T2_PULSE_COUNT = 0x20,
            ACR_T1_CONTINUOUS  = 0x40,
            ACR_T1_PB7_OUTPUT  = 0x80
        };

        // Timer 1 runtime state
        uint16_t timer1Counter;
        uint16_t timer1Latch;
        bool timer1Running;

        bool timer1JustLoaded;
        bool timer1ReloadPending;
        bool timer1InhibitIRQ;

        // PB7 output behavior for Timer 1 ACR modes
        bool timer1PB7Level;

        // Timer 2 runtime state
        uint16_t timer2Counter;
        uint16_t timer2Latch;
        bool timer2Running;

        bool timer2JustLoaded;
        bool timer2InhibitIRQ;

        // T2 low byte is latched on write to $08, then combined when $09 is written
        uint8_t timer2LowLatchByte;

        void timer1Tick();
        void timer2Tick();
        void syncTimer1Registers();
        void syncTimer2Registers();
};

#endif // DRIVEVIA6522_H
