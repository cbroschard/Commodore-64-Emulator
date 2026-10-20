// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef TPI6525_H
#define TPI6525_H

#include <cstdint>

class TPI6525
{
    public:
        TPI6525();
        ~TPI6525();

        void reset();

        uint8_t readRegister(uint8_t reg) const;
        void writeRegister(uint8_t reg, uint8_t value);

        void setPortAInput(uint8_t value);
        void setPortBInput(uint8_t value);
        void setPortCInput(uint8_t value);

        // Interrupt handling
        inline bool getIRQLine() const { return irqLine; }
        void setInterruptInput(uint8_t source, bool level);
        void latchInterrupt(uint8_t source);
        void clearInterrupt(uint8_t source);

    private:
        struct Registers
        {
            // Port output registers
            uint8_t portA;
            uint8_t portB;
            uint8_t portC;

            // Data direction registers
            uint8_t ddrA;
            uint8_t ddrB;
            uint8_t ddrC;

            // Control and Interrupt registers
            uint8_t cr;
            uint8_t air;
            uint8_t pendingInterrupt;

            // Interrupt mask registers when in interrupt mode
            uint8_t imr;
        } registers;

        enum class InterruptSource : uint8_t
        {
            I0 = 0,
            I1 = 1,
            I2 = 2,
            I3 = 3,
            I4 = 4
        };

        uint8_t portAInput;
        uint8_t portBInput;
        uint8_t portCInput;

        bool irqLine;
        uint8_t interruptInputs;

        void updateIRQ();

        // Helpers
        inline bool hasEnabledInterrupt() { return getEnabledInterrupts() != 0; }
        inline bool isInterruptMode() const { return (registers.cr & 0x01) != 0; }
        inline bool isPriorityMode() const { return (registers.cr & 0x02) != 0; }
        inline uint8_t getEnabledInterrupts() const { return (registers.imr & registers.pendingInterrupt); }
        uint8_t readPort(uint8_t output, uint8_t direction, uint8_t input) const;
        int getHighestPriorityInterrupt() const;
};

#endif // TPI6525_H
