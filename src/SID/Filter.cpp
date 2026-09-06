// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "SID/Filter.h"

Filter::Filter(double sampleRate) :
    model(SIDModel::MOS6581),
    sampleRate(sampleRate),
    cutoff(1000.0),
    resonance(0.0),
    f(0.0),
    q(0.0),
    lowPassOut(0.0),
    bandPassOut(0.0),
    mode(0)
{
    calculateCoefficients();
}

Filter::~Filter() = default;

double Filter::processSample(double input)
{
    const SIDModelProfile& profile = getSIDModelProfile(model);

    double drivenInput = input;

    // Model-specific analog-ish input drive.
    // 6581 gets more color; 8580 stays mostly clean.
    if (profile.filterDrive > 0.0)
    {
        drivenInput *= (1.0 + profile.filterDrive);

        if (profile.filterAsymmetry != 0.0)
            drivenInput += profile.filterAsymmetry * drivenInput * std::abs(drivenInput);

        drivenInput = std::tanh(drivenInput);
    }

    double output = 0.0;

    //
    // Run the state-variable filter at 2x the host sample rate.
    // This improves stability and high-cutoff behavior without
    // hard-clipping the internal filter state.
    //
    for (int step = 0; step < 2; ++step)
    {
        const double highPassOut = drivenInput - lowPassOut - q * bandPassOut;

        bandPassOut += f * highPassOut;
        lowPassOut  += f * bandPassOut;

        output = 0.0;

        if (mode & 0x01)
            output += lowPassOut;

        if (mode & 0x02)
            output += bandPassOut;

        if (mode & 0x04)
            output += highPassOut;
    }

    return std::clamp(output, -1.0, 1.0);
}

void Filter::setModel(SIDModel newModel)
{
    model = newModel;
    calculateCoefficients();
}

void Filter::reset()
{
    lowPassOut = 0.0;
    bandPassOut = 0.0;
    mode = 0;

    calculateCoefficients();
}

void Filter::setSampleRate(double sample)
{
    sampleRate = sample;
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

void Filter::calculateCoefficients()
{
    const SIDModelProfile& profile = getSIDModelProfile(model);
    const double filterSampleRate = sampleRate * 2.0;
    double fc = cutoff;

    fc = std::clamp(fc, profile.cutoffMinHz, profile.cutoffMaxHz);
    fc = std::clamp(fc, profile.cutoffMinHz, filterSampleRate * 0.45);
    f = 2.0 * std::sin(M_PI * fc / filterSampleRate);
    f = std::clamp(f, 0.0, 0.99);

    const double res = std::clamp(resonance * 15.0, 0.0, 15.0);

    if (model == SIDModel::MOS8580)
    {
        q = std::pow(
            2.0,
            (4.0 - res) / 8.0);
    }
    else
    {
        if (res <= 5.0)
            q = 1.41;
        else
            q = 8.0 / res;
    }
}
