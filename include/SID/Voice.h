// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef VOICE_H
#define VOICE_H

#include "Oscillator.h"
#include "Envelope.h"

class Voice
{
    public:
        Voice(double sampleRate);
        virtual ~Voice();

        void trigger();
        void release();

        // Getters
        const Oscillator& getOscillator() const;
        Oscillator& getOscillator();
        const Envelope& getEnvelope() const;
        Envelope& getEnvelope();

        // Setters
        void setSIDClockFrequency(double frequency);
        void setFrequency(uint16_t freqValue);
        void setPulseWidth(uint16_t pulseWidth);
        void setEnvelopeParameters(double attack, double decay, double sustain, double release);
        void setControl(uint8_t controlValue);
        void setFilterRouted(bool routed);

        // Generate the sample for the voice
        double generateVoiceSample();

        // Reset function for clean startup
        void reset();

    protected:

    private:
        // Initialize objects
        Oscillator osc;
        Envelope env;

        bool filterRouted;

        // Clock frequency
        double sidClockFrequency;
};

#endif // VOICE_H
