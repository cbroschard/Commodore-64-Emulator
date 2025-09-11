// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "SID/Filter.h"

Filter::Filter(double sampleRate) :
    sidClockFrequency(0.0),
    sampleRate(sampleRate),
    cutoff(1000.0),
    resonance(0.0),
    f(0.0), q(0.0),
    lowPassOut(0.0),
    bandPassOut(0.0),
    highPassOut(0.0),
    dcBlock(0.0),
    mode(0)
{
    calculateCoefficients();
}

Filter::~Filter() = default;

double Filter::processSample(double input)
{
    highPassOut = input - lowPassOut - q * bandPassOut;
    bandPassOut += f * highPassOut;
    bandPassOut = std::clamp(bandPassOut, -1.0, 1.0);
    lowPassOut  += f * bandPassOut;
    lowPassOut = std::clamp(lowPassOut, -1.0, 1.0);

    double output = 0.0;
    if (mode & 0x01) output += lowPassOut;
    if (mode & 0x02) output += bandPassOut;
    if (mode & 0x04) output += highPassOut;

    const double dcAlpha = 0.999; // Controls strength of filtering
    dcBlock = dcAlpha * dcBlock + (1.0 - dcAlpha) * output;
    //return output - dcBlock;
    return std::clamp(output - dcBlock, -1.0, 1.0);
}

void Filter::reset()
{
    f = 0.0;
    q = 0.0;
    lowPassOut = 0.0;
    bandPassOut = 0.0;
    highPassOut = 0.0;
    dcBlock = 0.0;
    mode = 0;
}

void Filter::setSampleRate(double sample)
{
    sampleRate = sample;
    calculateCoefficients();
}

void Filter::setSIDClockFrequency(double frequency)
{
    sidClockFrequency = frequency;
    calculateCoefficients();
}

void Filter::setCutoffFreq(double frequency)
{
    cutoff = frequency;
    // Recalculate coefficients when cutoff changes
    calculateCoefficients();
}

void Filter::setResonance(uint8_t res)
{
    // SID D417 bits 4–7 = 4-bit resonance value
    uint8_t r4 = (res >> 4) & 0x0F;
    resonance = static_cast<double>(r4) / 15.0;
    calculateCoefficients();
}

void Filter::setMode(uint8_t m)
{
    mode = m & 0x07;
}

void Filter::calculateCoefficients()
{
    double fc = std::clamp(cutoff, 30.0, sampleRate * 0.45);
    f = 2.0 * sin(M_PI * fc / sampleRate);

    // Clamp f to prevent potential instability at very high frequencies
    if (f > 0.99) f = 0.99;
    if (f < 0.0) f = 0.0;

    // Calculate 'q' based on resonance
    q = 1.0 - pow(resonance, 1.3);
    // Clamp q to ensure stable operation (resonance 0.0 to 1.0)
    if (q > 1.0) q = 1.0;
    if (q < 0.0) q = 0.0;
}
