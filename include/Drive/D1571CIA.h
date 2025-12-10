// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef D1571CIA_H
#define D1571CIA_H

#include <cstdint>
#include "Drive/DriveChips.h"
#include "Peripheral.h"

class D1571CIA : public DriveCIABase
{
    public:
        D1571CIA();
        virtual ~D1571CIA();

        uint8_t readRegister(uint16_t address);
        void writeRegister(uint16_t address, uint8_t value);

        inline void attachPeripheralInstance(Peripheral* parentPeripheral) { this->parentPeripheral = parentPeripheral; }

        void reset();
        void tick(uint32_t cycles);

        inline bool checkIRQActive() const { return (interruptStatus & registers.interruptEnable & 0x7F) != 0; }

        // ML Monitor
        inline ciaRegsView getRegsView() const override
        {
            return
                {
                    registers.portA,
                    registers.portB,
                    registers.ddrA,
                    registers.ddrB,
                    registers.timerALowByte,
                    registers.timerAHighByte,
                    registers.timerBLowByte,
                    registers.timerBHighByte,
                    registers.tod10th,
                    registers.todSeconds,
                    registers.todMinutes,
                    registers.todHours,
                    registers.serialData,
                    registers.interruptEnable,
                    registers.controlRegisterA,
                    registers.controlRegisterB,
                    timerACounter,
                    timerALatch,
                    timerBCounter,
                    timerBLatch
                };
        }

    protected:

    private:

        // Non-owning pointers
        Peripheral* parentPeripheral;

        //Interrupt handling
        enum InterruptBit : uint8_t
        {
            INTERRUPT_TIMER_A                   = 0x01,
            INTERRUPT_TIMER_B                   = 0x02,
            INTERRUPT_TOD_ALARM                 = 0x04,
            INTERRUPT_SERIAL_SHIFT_REGISTER     = 0x08,
            INTERRUPT_FLAG_LINE                 = 0x10
        };

        struct ciaRegs
        {
            // Ports and data direction
            uint8_t portA;
            uint8_t portB;
            uint8_t ddrA;
            uint8_t ddrB;

            // Timers
            uint8_t timerALowByte;
            uint8_t timerAHighByte;
            uint8_t timerBLowByte;
            uint8_t timerBHighByte;
            uint8_t tod10th;
            uint8_t todSeconds;
            uint8_t todMinutes;
            uint8_t todHours;

            // Serial/Interrupts/Control
            uint8_t serialData;
            uint8_t interruptEnable;
            uint8_t controlRegisterA;
            uint8_t controlRegisterB;
        } registers;

        uint16_t timerACounter;
        uint16_t timerALatch;
        uint16_t timerBCounter;
        uint16_t timerBLatch;
        bool timerARunning;
        bool timerBRunning;

        // TOD Alarm
        uint8_t todAlarm10th;
        uint8_t todAlarmSeconds;
        uint8_t todAlarmMinutes;
        uint8_t todAlarmHours;

        // Track IRQ
        uint8_t interruptStatus;
        void triggerInterrupt(InterruptBit bit);
};

#endif // D1571CIA_H
