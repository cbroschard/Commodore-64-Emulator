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
#include <cstddef>
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
    double filterDrive;
    double filterAsymmetry;
};

struct SIDCutoffPoint
{
    double x;   // normalized cutoff register value, 0.0 - 1.0
    double hz;  // mapped cutoff frequency
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
        1.15,     // resonanceCurvePower
        0.18,     // filterDrive
        0.06      // filterAsymmetry
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
        1.45,     // resonanceCurvePower
        0.03,     // filterDrive
        0.00      // filterAsymmetry
    };

    return (model == SIDModel::MOS8580) ? mos8580 : mos6581;
}

inline double interpolateSIDCutoffTable(
    double x,
    const SIDCutoffPoint* table,
    size_t count)
{
    if (!table || count == 0)
        return 30.0;

    x = std::clamp(x, 0.0, 1.0);

    if (x <= table[0].x)
        return table[0].hz;

    for (size_t i = 1; i < count; ++i)
    {
        if (x <= table[i].x)
        {
            const SIDCutoffPoint& a = table[i - 1];
            const SIDCutoffPoint& b = table[i];

            const double span = b.x - a.x;
            if (span <= 0.0)
                return b.hz;

            const double t = (x - a.x) / span;
            return a.hz + (b.hz - a.hz) * t;
        }
    }

    return table[count - 1].hz;
}

inline double mapSIDCutoff11BitToHzTable(uint16_t cutoff11bit, SIDModel model)
{
    static constexpr SIDCutoffPoint mos6581Table[] =
    {
        {0.00, 30.0},
        {0.10, 55.0},
        {0.20, 100.0},
        {0.30, 220.0},
        {0.40, 500.0},
        {0.50, 950.0},
        {0.60, 1700.0},
        {0.70, 3000.0},
        {0.80, 5000.0},
        {0.90, 7600.0},
        {1.00, 10500.0}
    };

    static constexpr SIDCutoffPoint mos8580Table[] =
    {
        {0.00, 30.0},
        {0.10, 350.0},
        {0.20, 850.0},
        {0.30, 1600.0},
        {0.40, 2800.0},
        {0.50, 4300.0},
        {0.60, 6100.0},
        {0.70, 8000.0},
        {0.80, 10000.0},
        {0.90, 12200.0},
        {1.00, 14000.0}
    };

    const double x =
        std::clamp(static_cast<double>(cutoff11bit & 0x07FF) / 2047.0, 0.0, 1.0);

    if (model == SIDModel::MOS8580)
    {
        return interpolateSIDCutoffTable(
            x,
            mos8580Table,
            sizeof(mos8580Table) / sizeof(mos8580Table[0]));
    }

    return interpolateSIDCutoffTable(
        x,
        mos6581Table,
        sizeof(mos6581Table) / sizeof(mos6581Table[0]));
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
