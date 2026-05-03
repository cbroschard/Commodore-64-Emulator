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
#include <string>
#include "SID/SIDModelProfile.h"

class Oscillator
{
    public:
        Oscillator(double sampleRate);
        virtual ~Oscillator();

        // Getters
        inline uint32_t getNoiseLFSR() const { return noiseLFSR; }
        inline double getPhase() const { return phase; }
        inline bool getPhaseOverflow() const { return phaseOverflow; }
        inline uint8_t getControl() const { return control; }
        inline double getFrequency() const { return frequency; }

        // Setters
        inline void setPhaseOverflow(bool value) { phaseOverflow = value; }
        inline void setNoiseLFSR(uint32_t value) { noiseLFSR = value & 0x7FFFFF; }
        inline void setSampleRate(double sample) { sampleRate = sample; }
        inline void setPulseWidth(double width) { pulseWidth = width; }
        inline void setSyncSource(Oscillator* source) { syncSource  = source; }
        inline void setRingSource(Oscillator* source) { ringSource = source; }
        void setAccumulator24(uint32_t value);
        void setPhase(double value);
        void setSIDClockFrequency(double frequency);
        void setFrequency(uint16_t freqRegValue);
        void setControl(uint8_t controlValue);
        uint8_t readOutput8() const;

        // Generate the sample for output
        double generateMixedSample();

        // Reset function
        void reset();

        inline void setSIDModel(SIDModel model) { sidModel_ = model; }

        // Reset the phase
        void resetPhase();

        // Update the phase accumulator for the next cycle
        void updatePhase();

        void clock(double sidCycles);
        double outputSample();

        uint32_t getAccumulator24() const { return accumulator24 & 0x00FFFFFF; }
        uint16_t getFrequencyReg() const { return frequencyReg; }

        // ML Monitor
        double getAccumulatorPhase() const;
        std::string dumpDebug(uint16_t freqReg, uint16_t pulseWidthReg) const;

    protected:

    private:

        Oscillator* syncSource = nullptr;
        Oscillator* ringSource = nullptr;

        SIDModel sidModel_ = SIDModel::MOS6581;

        uint32_t noiseLFSR;
        double sampleRate;
        double phase; // Phase accumulator, normalized to [0, 1)
        double sidClockFrequency;
        double frequency; // Frequency in Hz
        double pulseWidth; // Pulse width for pulse waveform
        bool phaseOverflow;
        uint8_t control; // Control value from relevant voice

        uint32_t accumulator24;
        uint16_t frequencyReg;

        uint16_t getAccumulatorSaw12() const;
        uint16_t getAccumulatorTriangle12() const;
        uint16_t getAccumulatorPulse12() const;

        // Helpers
        uint16_t getTriangleBits();
        uint16_t getSawBits();
        uint16_t getPulseBits();
        uint16_t getNoiseBits();
        double convertToFloat(uint16_t sampleBits);

        void clockNoiseLFSR();
        uint16_t getNoiseOutputBits() const;
};

#endif // OSCILLATOR_H
