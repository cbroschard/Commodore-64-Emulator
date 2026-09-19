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
#include <functional>
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

        void tick(uint32_t elapsedCycles);

        uint8_t read(uint8_t rs);
        void write(uint8_t rs, uint8_t value);

        uint8_t peek(uint8_t rs) const;

        void setPortAInputs(uint8_t value);
        void setPortBInputs(uint8_t value);

        inline uint8_t getPortAOutputLatch() const { return registers.ora; }
        inline uint8_t getPortBOutputLatch() const { return registers.orb; }

        inline uint8_t getPortADirection() const { return registers.ddra; }
        inline uint8_t getPortBDirection() const { return registers.ddrb; }

        inline bool getCA2Output() const { return ca2Output; }
        inline bool getCB2Output() const { return cb2Output; }

        inline bool getIRQA() const { return irqA; }
        inline bool getIRQB() const { return irqB; }

        void setCA1(bool level);
        void setCA2Input(bool level);

        void setCB1(bool level);
        void setCB2Input(bool level);

        void setResetLine(bool high);

        inline void setCA2OutputCallback(std::function<void(bool)> callback) { ca2OutputCallback = std::move(callback); }
        inline void setCB2OutputCallback(std::function<void(bool)> callback) { cb2OutputCallback = std::move(callback); }

        inline void setIRQACallback(std::function<void(bool)> callback) { irqACallback = std::move(callback); }
        inline void setIRQBCallback(std::function<void(bool)> callback) { irqBCallback = std::move(callback); }

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
        bool ca2Input;
        bool ca2Output;
        bool ca2PulseActive;

        uint32_t ca2PulseCycles;

        bool cb1;
        bool cb2Input;
        bool cb2Output;
        bool cb2PulseActive;

        uint32_t cb2PulseCycles;

        bool irqA1Flag;
        bool irqA2Flag;
        bool irqB1Flag;
        bool irqB2Flag;

        bool irqA;
        bool irqB;

        bool resetLine;

        std::function<void(bool)> ca2OutputCallback;
        std::function<void(bool)> cb2OutputCallback;

        std::function<void(bool)> irqACallback;
        std::function<void(bool)> irqBCallback;

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

        void setCA2Output(bool level);
        void setCB2Output(bool level);

        void setIRQAOutput(bool level);
        void setIRQBOutput(bool level);

        void updateIRQA();
        void updateIRQB();

        // Helpers
        inline C2Mode getCA2Mode() const { return static_cast<C2Mode>((registers.cra >> 3) & 0x07); }
        inline C2Mode getCB2Mode() const { return static_cast<C2Mode>((registers.crb >> 3) & 0x07); }
};

#endif // MC6821_H
