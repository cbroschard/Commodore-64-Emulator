// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
// include/SID/SIDModelProfile.h
#ifndef SID_MODEL_PROFILE_H
#define SID_MODEL_PROFILE_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include "Common/SIDModel.h"

struct SIDModelProfile
{
    // Output / mixer profile
    double directGain;
    double filterInputGain;
    double filterOutputGain;
    double volumeDacGain;
    double outputBias;
    double softClipDrive;

    // Oscillator DAC profile
    double waveformDacGamma;
    double waveformDacBias;

    // Filter profile
    double cutoffMinHz;
    double cutoffMaxHz;
    double cutoffCurvePower;
    double resonanceCurvePower;
};

inline const SIDModelProfile& getSIDModelProfile(SIDModel model)
{
    static const SIDModelProfile mos6581 =
    {
        // Output / mixer profile
        1.05,   // directGain
        1.15,   // filterInputGain
        1.10,   // filterOutputGain
        0.060,  // volumeDacGain
        0.010,  // outputBias
        1.20,   // softClipDrive

        // Oscillator DAC profile
        1.08,   // waveformDacGamma
        0.015,  // waveformDacBias

        // Filter profile
        30.0,     // cutoffMinHz
        11000.0,  // cutoffMaxHz
        2.20,     // cutoffCurvePower
        1.15      // resonanceCurvePower
    };

    static const SIDModelProfile mos8580 =
    {
        // Output / mixer profile
        0.95,   // directGain
        0.90,   // filterInputGain
        0.95,   // filterOutputGain
        0.015,  // volumeDacGain
        0.000,  // outputBias
        1.00,   // softClipDrive

        // Oscillator DAC profile
        1.00,   // waveformDacGamma
        0.000,  // waveformDacBias

        // Filter profile
        30.0,     // cutoffMinHz
        14000.0,  // cutoffMaxHz
        1.35,     // cutoffCurvePower
        1.45      // resonanceCurvePower
    };

    return (model == SIDModel::MOS8580) ? mos8580 : mos6581;
}

inline double applySIDWaveformDac(uint16_t sampleBits, SIDModel model)
{
    sampleBits &= 0x0FFF;

    const SIDModelProfile& profile = getSIDModelProfile(model);

    double x = static_cast<double>(sampleBits) / 4095.0;

    if (profile.waveformDacGamma != 1.0)
        x = std::pow(x, profile.waveformDacGamma);

    return std::clamp((x * 2.0) - 1.0 + profile.waveformDacBias, -1.0, 1.0);
}

inline double mapSIDCutoff11BitToHz(uint16_t cutoff11bit, SIDModel model)
{
    const SIDModelProfile& profile = getSIDModelProfile(model);

    const double x =
        std::clamp(static_cast<double>(cutoff11bit) / 2047.0, 0.0, 1.0);

    const double curve = std::pow(x, profile.cutoffCurvePower);

    return profile.cutoffMinHz +
           curve * (profile.cutoffMaxHz - profile.cutoffMinHz);
}

#endif // SID_MODEL_PROFILE_H
