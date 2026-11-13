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

        void saveState(StateWriter& wrtr) const;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr);

        void reset();
        void tick(uint32_t cycles);

        uint8_t readRegister(uint16_t address);
        void writeRegister(uint16_t address, uint8_t value);

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

        uint8_t inputA;
        uint8_t inputB;

        std::array<uint8_t, 128> ram{};
};

#endif // MOS6532_H
