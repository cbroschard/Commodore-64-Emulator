// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef FILTER_H
#define FILTER_H

#define _USE_MATH_DEFINES

#include <algorithm>
#include <cstdint>
#include <math.h>

class Filter
{
    public:
        Filter(double sampleRate);
        virtual ~Filter();

        // Process one audio sample through the filter.
        double processSample(double input);

        // Reset function
        void reset();

        // Setters
        void setSampleRate(double sample);
        void setSIDClockFrequency(double frequency);
        void setCutoffFreq(double frequency);
        void setResonance(uint8_t res);
        void setMode(uint8_t m);

    protected:


    private:

        double sidClockFrequency;
        double sampleRate;
        double cutoff;    // Cutoff frequency in Hz.
        double resonance; // Normalized resonance (0.0 - 1.0).
        double f, q;      // Filter coefficients.
        double lowPassOut;
        double bandPassOut;
        double highPassOut;
        double dcBlock;
        uint8_t mode; // bits 0-2 from D417

        // Helper to recalculate filter coefficients when parameters change
        void calculateCoefficients();
};

#endif // FILTER_H
