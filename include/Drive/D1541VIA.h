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
class Peripheral;
class IECBUS;

#include <cstdint>
#include "Logging.h"

class D1541VIA
{
    public:
        D1541VIA();
        virtual ~D1541VIA();

        // Allow VIA1 and VIA2 to define their role
        enum class VIARole
        {
            Unknown,
            VIA1_DataHandler,
            VIA2_AtnMonitor
        };

        // Pointers
        void attachLoggingInstance(Logging* logger);
        void attachPeripheralInstance(Peripheral* parentPeripheral, VIARole role);

        // Advance VIA via tick
        void tick();

        // Reset all
        void reset();

        // Register access
        uint8_t readRegister(uint16_t address);
        void writeRegister(uint16_t address, uint8_t value);

    protected:

    private:

        // Non-owning pointers
        Logging* logger = nullptr;
        Peripheral* parentPeripheral = nullptr;

        VIARole viaRole;

        // bitâ€‘masks for IFR
        static constexpr uint8_t IFR_T1 = 1 << 0;
        static constexpr uint8_t IFR_T2 = 1 << 1;
        static constexpr uint8_t IFR_SR = 1 << 2;
        static constexpr uint8_t IFR_IRQ  = 1 << 7; // global interrupt flag

        // bitâ€‘masks for IER (interrupt enables)
        static constexpr uint8_t IER_T1 = 1 << 0;
        static constexpr uint8_t IER_T2 = 1 << 1;
        static constexpr uint8_t IER_SR = 1 << 2;

        // I/O registers
        uint8_t portA;
        uint8_t portB;
        uint8_t ddrA;
        uint8_t ddrB;

        // Timer registers
        uint8_t timer1CounterLow;
        uint8_t timer1CounterHigh;
        uint8_t timer1LatchLow;
        uint8_t timer1LatchHigh;
        uint8_t timer2Counter;

        // Control registers
        uint8_t shiftRegister;
        uint8_t auxiliaryControlRegister;
        uint8_t peripheralControlRegister;

        // Interrupt registers
        uint8_t interruptFlagRegister;
        uint8_t interruptEnableRegister;

        // Reserved registers
        uint8_t reserved1;
        uint8_t reserved2;

        int srCount;
};

#endif // D1541VIA_H
