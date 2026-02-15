// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef OSCILLATOR_H
#define OSCILLATOR_H

#include <algorithm>
#include <cmath>
#include <cstdint>

class Oscillator
{
    public:
        Oscillator(double sampleRate);
        virtual ~Oscillator();

        // Getters
        inline uint32_t getNoiseLFSR() const { return noiseLFSR; }
        inline double getPhase() const { return phase; }
        inline bool didOverflow() const { return phaseOverflow; }
        inline uint8_t getControl() const { return control; }
        inline double getFrequency() const { return frequency; }

        // Setters
        inline void setNoiseLFSR(uint32_t value) { noiseLFSR = value & 0x7FFFFF; }
        inline void setSampleRate(double sample) { sampleRate = sample; }
        inline void setSIDClockFrequency(double frequency) { sidClockFrequency = frequency; }
        inline void setFrequency(uint16_t freqRegValue) { frequency = (static_cast<double>(freqRegValue) * sidClockFrequency) / 16777216.0; }
        inline void setPulseWidth(double width) { pulseWidth = width; }
        void setSyncSource(Oscillator* source) { syncSource  = source; }
        void setRingSource(Oscillator* source) { ringSource = source; }
        void setControl(uint8_t controlValue);

        // Generate the sample for output
        double generateMixedSample();

        // Reset function
        void reset();

        // Reset the phase
        inline void resetPhase() { phase = 0.0; }

        // Update the phase accumulator for the next cycle
        void updatePhase();

    protected:

    private:

        Oscillator* syncSource = nullptr;
        Oscillator* ringSource = nullptr;

        uint32_t noiseLFSR;
        double sampleRate;
        double phase; // Phase accumulator, normalized to [0, 1)
        double sidClockFrequency;
        double frequency; // Frequency in Hz
        double pulseWidth; // Pulse width for pulse waveform
        bool phaseOverflow;
        uint8_t control; // Control value from relevant voice

        // Helpers
        uint16_t getTriangleBits();
        uint16_t getSawBits();
        uint16_t getPulseBits();
        uint16_t getNoiseBits();
        double convertToFloat(uint16_t sampleBits);
};

#endif // OSCILLATOR_H
