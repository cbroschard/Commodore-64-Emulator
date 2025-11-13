// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef D1581CIA_H
#define D1581CIA_H

#include "Peripheral.h"

class D1581CIA
{
    public:
        D1581CIA();
        virtual ~D1581CIA();

        inline void attachPeripheralInstance(Peripheral* parentPeripheral) { this->parentPeripheral = parentPeripheral; }

        // API access
        uint8_t readRegister(uint16_t address);
        void writeRegister(uint16_t address, uint8_t value);

    protected:

    private:

        // Non-owning pointers
        Peripheral* parentPeripheral;

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

        enum CIA1581_PRA : uint8_t
        {
            PRA_SIDE    = 1u << 0, // 0 = side 0, 1 = side 1
            PRA_DRVRDY  = 1u << 1, // 1 = drive ready (input)
            PRA_MOTOR   = 1u << 2, // 0 = on, 1 = off
            PRA_DEVSW2  = 1u << 3, // device switch 2 (right)
            PRA_DEVSW1  = 1u << 4, // device switch 1 (left)
            PRA_ERRLED  = 1u << 5, // red LED
            PRA_ACTLED  = 1u << 6, // green LED
            PRA_DSKCH   = 1u << 7  // disk present/change
        };

        enum CIA1581_PRB : uint8_t
        {
            PRB_DATAIN  = 1u << 0,
            PRB_DATOUT  = 1u << 1,
            PRB_CLKIN   = 1u << 2,
            PRB_CLKOUT  = 1u << 3,
            PRB_ATNACK  = 1u << 4,
            PRB_BUSDIR  = 1u << 5,
            PRB_WRTPRO  = 1u << 6,
            PRB_ATNIN   = 1u << 7
        };
};

#endif // D1581CIA_H
