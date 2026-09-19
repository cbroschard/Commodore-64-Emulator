// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef MC6821_H
#define MC6821_H

#include <cstdint>
#include "StateReader.h"
#include "StateWriter.h"

class MC6821
{
    public:
        MC6821();
        virtual ~MC6821();

        void saveState(StateWriter& wrtr) const;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr);

        void reset();

        uint8_t read(uint8_t rs);
        void write(uint8_t rs, uint8_t value);

        uint8_t peek(uint8_t rs) const;

    private:
        enum class C2Mode : uint8_t
        {
            InputFallingNoIRQ = 0,
            InputFallingIRQ   = 1,
            InputRisingNoIRQ  = 2,
            InputRisingIRQ    = 3,
            Handshake         = 4,
            Pulse             = 5,
            ForceLow          = 6,
            ForceHigh         = 7
        };

        struct Registers
        {
            uint8_t ora  = 0x00;
            uint8_t orb  = 0x00;

            uint8_t ddra = 0x00;
            uint8_t ddrb = 0x00;

            uint8_t cra  = 0x00;
            uint8_t crb  = 0x00;
        };

        Registers registers;

        uint8_t externalPinsA;
        uint8_t externalPinsB;

        bool ca1;
        bool ca2;
        bool cb1;
        bool cb2;

        bool irqA1Flag;
        bool irqA2Flag;
        bool irqB1Flag;
        bool irqB2Flag;

        bool resetLine;

        uint8_t readPortA();
        uint8_t readPortB();

        uint8_t peekPortA() const;
        uint8_t peekPortB() const;

        uint8_t readCRA() const;
        uint8_t readCRB() const;

        void writePortA(uint8_t value);
        void writePortB(uint8_t value);

        void writeCRA(uint8_t value);
        void writeCRB(uint8_t value);

        void updateIRQA();
        void updateIRQB();

        void setResetLine(bool high);
};

#endif // MC6821_H
