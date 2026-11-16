// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef MOS6530_H
#define MOS6530_H

#include <array>
#include <cstdint>
#include "StateReader.h"
#include "StateWriter.h"

class MOS6530
{
    public:
        MOS6530();
        ~MOS6530();

        void reset();

        void save(StateWriter& wrtr) const;
        bool load(StateReader& rdr);

        void tick(uint32_t cycles);

        uint8_t read(uint8_t address);
        void write(uint8_t address, uint8_t value);

        uint8_t peek(uint8_t address) const;

        uint8_t readRAM(uint8_t address) const;
        void writeRAM(uint8_t address, uint8_t value);

        void setPortAInput(uint8_t value);
        void setPortBInput(uint8_t value);

        inline uint8_t getPortAOutput() const { return registers.portA; }
        inline uint8_t getPortBOutput() const { return registers.portB; }

        inline uint8_t getDDRA() const { return registers.ddrA; }
        inline uint8_t getDDRB() const { return registers.ddrB; }

        inline bool isIRQActive() const { return registers.timerIRQEnabled && (registers.interruptFlags & 0x80) != 0; }

    private:
        static constexpr size_t RAM_SIZE = 0x40;

        struct Registers
        {
            // I/O ports
            uint8_t portA = 0x00;
            uint8_t ddrA = 0x00;
            uint8_t portB = 0x00;
            uint8_t ddrB = 0x00;

            // Timer
            uint8_t timer = 0x00;

            // Interrupt state
            uint8_t interruptFlags = 0x00;
            bool timerIRQEnabled = false;
        } registers;

        // Timer runtime state
        uint16_t timerPrescaler;
        uint16_t timerDivider;
        bool timerRunning;
        bool timerUnderflowed;

        // External port inputs
        uint8_t portAInput;
        uint8_t portBInput;

        std::array<uint8_t, RAM_SIZE> ram{};
};

#endif // MOS6530_H
