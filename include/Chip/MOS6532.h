// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef MOS6532_H
#define MOS6532_H

#include <array>
#include <cstdint>
#include "StateReader.h"
#include "StateWriter.h"

class MOS6532
{
    public:
        MOS6532();
        virtual ~MOS6532();

        void save(StateWriter& wrtr) const;
        bool load(StateReader& rdr);

        void reset();
        void tick(uint32_t cycles);

        uint8_t read(uint8_t address);
        void write(uint8_t address, uint8_t value);

        uint8_t peek(uint8_t address) const;

        uint8_t readRAM(uint8_t address) const;
        void writeRAM(uint8_t address, uint8_t value);

        // External port inputs
        inline void setPortAInput(uint8_t value) { portAInput = value; }
        inline void setPortBInput(uint8_t value) { portBInput = value; }

        // Output state
        inline uint8_t getPortAOutput() const { return registers.portA; }
        inline uint8_t getPortBOutput() const { return registers.portB; }

        inline uint8_t getDDRA() const { return registers.ddrA; }
        inline uint8_t getDDRB() const { return registers.ddrB; }

        // Interrupt output
        bool isIRQActive() const;

    private:
        struct Registers
        {
            // I/O ports
            uint8_t portA = 0;
            uint8_t ddrA = 0;
            uint8_t portB = 0;
            uint8_t ddrB = 0;

            // Timer
            uint8_t timer = 0;
            uint8_t interruptFlags = 0;

            // Interrupt configuration
            bool timerIRQEnabled = false;
            bool pa7IRQEnabled = false;
            bool pa7PositiveEdge = false;
        } registers;

        uint16_t timerPrescaler;
        uint16_t timerDivider;

        bool timerRunning;
        bool timerUnderflowed;

        bool previousPA7;

        uint8_t portAInput;
        uint8_t portBInput;

        std::array<uint8_t, 128> ram{};
};

#endif // MOS6532_H
