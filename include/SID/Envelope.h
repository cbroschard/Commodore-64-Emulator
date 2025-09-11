// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef ENVELOPE_H
#define ENVELOPE_H

#include <cmath>

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

        // Setters
        void setSampleRate(double sample);
        void setParameters(double attack, double decay, double sustain, double release);

        // Getters
        double getLevel() const;

        // Reset envelope
        void reset();

    protected:

    private:

        double sampleRate; // Audio sample rate passed in by SID
        State state;         // Current envelope state
        double level;        // Current amplitude level (0.0 to 1.0)

        // Envelope timing parameters (in seconds)
        double attackTime;
        double decayTime;
        double sustainLevel;
        double releaseTime;

        // Envelope coefficients
        double attackCoeff;
        double decayCoeff;
        double releaseCoeff;
};

#endif // ENVELOPE_H
