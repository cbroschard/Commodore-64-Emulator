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

class D1571CIA
{
    public:
        D1571CIA();
        virtual ~D1571CIA();

        uint8_t readRegister(uint16_t address);
        void writeRegister(uint16_t address, uint8_t value);

    protected:

    private:

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
            uint8_t interruptControl;
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

};

#endif // D1571CIA_H
