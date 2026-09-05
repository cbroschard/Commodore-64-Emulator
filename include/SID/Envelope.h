// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef ENVELOPE_H
#define ENVELOPE_H

#include <cstdint>
#include <cmath>
#include <string>

class Envelope
{
    public:
        Envelope();
        virtual ~Envelope();

        // Define the different states for the envelope.
        enum class State : uint8_t
        {
            Attack       = 0,
            DecaySustain = 1,
            Release      = 2
        };

        void trigger(); // Trigger the envelope (key pressed), attack phase
        void release(); // Release the envelope (key pressed), release phase

        bool isIdle() const;

        void clock(double sidCycles);
        double output() const;

        // Getters
        inline State getState() const { return state; }
        inline uint8_t getAttackRate() const { return attackRate; }
        inline uint8_t getDecayRate() const { return decayRate; }
        inline uint8_t getSustainRate() const { return sustainRate; }
        inline uint8_t getReleaseRate() const { return releaseRate; }
        inline uint32_t getExponentialCounter() const { return exponentialCounter; }
        inline uint32_t getExponentialPeriod() const { return exponentialPeriod; }
        inline uint16_t getRateCounter() const { return rateCounter; }
        inline bool getHoldZero() const { return holdZero; }

        uint8_t readOutput8() const;

        // Setters
        inline void setExponentialCounter(uint32_t value) { exponentialCounter = value; }
        inline void setExponentialPeriod(uint32_t value) { exponentialPeriod = std::max<uint32_t>(1, value); }
        inline void setState(Envelope::State value) { state = value; }
        inline void setRateCounter(uint16_t value) { rateCounter = value; }
        inline void setHoldZero(bool state) { holdZero = state; }

        void setADSR(uint8_t attack, uint8_t decay, uint8_t sustain, uint8_t release);
        void setEnvelopeCounter(uint8_t value);

        // Helpers
        static std::string stateToString(State s);

        // Reset envelope
        void reset();

        // ML Monitor
        std::string dumpDebug() const;

    private:
        State state;            // Current envelope state

        uint8_t attackRate;
        uint8_t decayRate;
        uint8_t sustainRate;
        uint8_t releaseRate;

        uint8_t envCounter;

        uint32_t exponentialCounter;
        uint32_t exponentialPeriod;

        uint8_t sustainCounter;
        uint16_t rateCounter;

        bool holdZero;

        // Helpers
        void updateExponentialPeriod();
        uint16_t getRatePeriod(uint8_t rate) const;
};

#endif // ENVELOPE_H
