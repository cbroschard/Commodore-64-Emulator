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
        Envelope(double sampleRate);
        virtual ~Envelope();

        // Define the different states for the envelope.
        enum class State
        {
            Idle,
            Attack,
            Decay,
            Sustain,
            Release
        };

        void trigger(); // Trigger the envelope (key pressed), attack phase
        void release(); // Release the envelope (key pressed), release phase

        double processSample();
        bool isIdle() const;

        void clock(double sidCycles);
        double output() const;

        // Getters
        inline double getLevel() const { return level; }
        inline State getState() const { return state; }
        inline uint8_t getAttackRate() const { return attackRate; }
        inline uint8_t getDecayRate() const { return decayRate; }
        inline uint8_t getSustainRate() const { return sustainRate; }
        inline uint8_t getReleaseRate() const { return releaseRate; }
        inline uint32_t getExponentialCounter() const { return exponentialCounter; }
        inline uint32_t getExponentialPeriod() const { return exponentialPeriod; }
        uint8_t readOutput8() const;

        // Setters
        inline void setExponentialCounter(uint32_t value) { exponentialCounter = value; }
        inline void setExponentialPeriod(uint32_t value) { exponentialPeriod = std::max<uint32_t>(1, value); }
        void setSIDClockFrequency(double frequency);
        void setLevel(double newLevel);
        inline void setState(Envelope::State value) { state = value; }
        void setSampleRate(double sample);
        void setParameters(double attack, double decay, double sustain, double release);
        void setADSR(uint8_t attack, uint8_t decay, uint8_t sustain, uint8_t release);

        // Helpers
        static std::string stateToString(State s);

        // Reset envelope
        void reset();

        // ML Monitor
        std::string dumpDebug() const;

    protected:

    private:
        double sidClockFrequency;

        double sampleRate;      // Audio sample rate passed in by SID
        State state;            // Current envelope state
        double level;           // Current amplitude level (0.0 to 1.0)

        // Envelope timing parameters (in seconds)
        double attackTime;
        double decayTime;
        double sustainLevel;
        double releaseTime;

        uint8_t attackRate;
        uint8_t decayRate;
        uint8_t sustainRate;
        uint8_t releaseRate;

        uint8_t envCounter;
        double stepAccumulator;

        double attackStepCycles;
        double decayStepCycles;
        double releaseStepCycles;

        uint32_t exponentialCounter;
        uint32_t exponentialPeriod;

        uint8_t sustainCounter;

        // Helpers
        void syncLevelFromCounter();
        void updateExponentialPeriod();
};

#endif // ENVELOPE_H
