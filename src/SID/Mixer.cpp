// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "SID/Mixer.h"

Mixer::Mixer() = default;

Mixer::~Mixer() = default;

double Mixer::mixSamples(const std::vector<double>& voiceSamples)
{
    if (voiceSamples.empty()) return 0.0;

    //double perVoiceGain = 0.5; // ~–6 dB per voice
    double perVoiceGain = 0.707;
    double sum = 0.0;
    for (double s : voiceSamples)
    {
        sum += s * perVoiceGain;
    }

    // soft-clip
    sum = sum / (1.0 + std::abs(sum));

    return sum;
}
