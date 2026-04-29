// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "SID/Oscillator.h"

static inline double polyBLEP(double t, double dt) {
    if (t < dt)
    {
        double x = t/dt;
        return x + x - x*x - 1.0;
    }
    else if (t > 1.0 - dt)
    {
        double x = (t - 1.0)/dt;
        return x*x + x + x + 1.0;
    }
    return 0.0;
}

Oscillator::Oscillator(double sampleRate) :
    syncSource(nullptr),
    ringSource(nullptr),
    noiseLFSR(0x7FFFFF),
    sampleRate(sampleRate),
    phase(0.0),
    sidClockFrequency(0.0),
    frequency(0.0),
    pulseWidth(0.5),
    phaseOverflow(false),
    control(0)
{

}

Oscillator::~Oscillator() = default;

inline double Oscillator::convertToFloat(uint16_t sampleBits)
{
    // Convert 12-bit sample (0-4095) to float range [-1.0, +1.0]
    return (static_cast<double>(sampleBits) / 2047.5) - 1.0;
}

void Oscillator::setControl(uint8_t controlValue)
{
    if ((controlValue & 0x08) && !(control & 0x08))
    {
        phase = 0.0;
        noiseLFSR = 0x7FFFFF;
    }
    control = controlValue;
}

uint8_t Oscillator::readOutput8() const
{
    if ((control & 0xF0) == 0)
        return 0x00;

    const double t = phase - std::floor(phase);

    uint16_t mixedBits = 0x0FFF;
    bool waveformSelected = false;

    if (control & 0x10) // Triangle
    {
        double tri = std::fabs(2.0 * (t - 0.5));

        if ((control & 0x04) && ringSource)
        {
            if (ringSource->getPhase() >= 0.5)
                tri = 1.0 - tri;
        }

        tri = std::clamp(tri, 0.0, 1.0);
        mixedBits &= static_cast<uint16_t>(tri * 4095.0);
        waveformSelected = true;
    }

    if (control & 0x20) // Sawtooth
    {
        const uint16_t sawBits = static_cast<uint16_t>(std::clamp(t, 0.0, 1.0) * 4095.0);

        if (!waveformSelected)
            mixedBits = sawBits;
        else
            mixedBits &= sawBits;

        waveformSelected = true;
    }

    if (control & 0x40) // Pulse
    {
        const uint16_t pulseBits = (t < pulseWidth) ? 0x0FFF : 0x0000;

        if (!waveformSelected)
            mixedBits = pulseBits;
        else
            mixedBits &= pulseBits;

        waveformSelected = true;
    }

    if (control & 0x80) // Noise
    {
        // Non-mutating read of current noise register state.
        const uint16_t noiseBits = static_cast<uint16_t>((noiseLFSR >> 11) & 0x0FFF);

        if (!waveformSelected)
            mixedBits = noiseBits;
        else
            mixedBits &= noiseBits;

        waveformSelected = true;
    }

    if (!waveformSelected)
        return 0x00;

    return static_cast<uint8_t>((mixedBits >> 4) & 0xFF);
}

double Oscillator::generateMixedSample()
{
    if ((control & 0xF0) == 0)
    {
        return 0.0;
    }

    updatePhase();

    uint16_t mixedBits = 0xFFFF; // Start with all bits set for the AND operation
    bool waveformSelected = false;

    if (control & 0x10) // Triangle
    {
        mixedBits &= getTriangleBits();
        waveformSelected = true;
    }
    if (control & 0x20) // Sawtooth
    {
        if (!waveformSelected)
        {
             mixedBits = getSawBits(); // If this is the first selected, initialize
        }
        else
        {
             mixedBits &= getSawBits(); // Otherwise, AND
        }
        waveformSelected = true;
    }
    if (control & 0x40) // Pulse
    {
        if (!waveformSelected)
        {
             mixedBits = getPulseBits();
             waveformSelected = true;
        }
        else
        {
            mixedBits &= getPulseBits();
        }
    }
    if (control & 0x80) // Noise
    {
        if (!waveformSelected)
        {
             mixedBits = getNoiseBits();
             waveformSelected = true;
        }
        else
        {
            mixedBits &= getNoiseBits();
        }
    }

    // If F0 is non-zero but no individual waveform bit was set (unusual state)
    if (!waveformSelected && (control & 0xF0) != 0)
    {
        return 0.0; // Treat as silence
    }

    return convertToFloat(mixedBits);
}

void Oscillator::reset()
{
    phase = 0.0;
    noiseLFSR = 0x7FFFFF;
    phaseOverflow = false;
}

uint16_t Oscillator::getTriangleBits()
{
    double dt = frequency / sampleRate;
    double t  = phase - floor(phase); // wrap to [0..1)

    // basic triangle: peak at t=0.5
    double tri = fabs(2.0 * (t - 0.5));

    tri -= polyBLEP(t, dt);
    tri += polyBLEP(fmod(t+0.5, 1.0), dt);

    // optional ring-mod
    if ((control & 0x04) && ringSource)
    {
        if (ringSource->getPhase() >= 0.5)
            tri = 1.0 - tri;
    }

    // clamp & convert to 12-bit
    tri = std::clamp(tri, 0.0, 1.0);
    return static_cast<uint16_t>(tri * 4095.0);
}

uint16_t Oscillator::getSawBits()
{
    // Sawtooth waveform from 0 to 4095
    double dt  = frequency / sampleRate;
    double t   = phase - floor(phase);
    double saw = t - polyBLEP(t, dt);
    return uint16_t(saw * 4095.0);
}

uint16_t Oscillator::getPulseBits()
{
    const double dt = frequency / sampleRate;
    const double t  = phase - std::floor(phase);

    if (pulseWidth <= 0.0)
        return 0x0000;

    if (pulseWidth >= 1.0)
        return 0x0FFF;

    double value = (t < pulseWidth) ? 1.0 : 0.0;

    // Rising edge at phase wrap.
    value -= polyBLEP(t, dt);

    // Falling edge at pulse-width threshold.
    double t2 = t - pulseWidth;
    if (t2 < 0.0)
        t2 += 1.0;

    value += polyBLEP(t2, dt);

    value = std::clamp(value, 0.0, 1.0);
    return static_cast<uint16_t>(value * 4095.0);
}

uint16_t Oscillator::getNoiseBits()
{
    // Update the 23-bit LFSR
    uint32_t bit22 = (noiseLFSR >> 22) & 1;
    uint32_t bit17 = (noiseLFSR >> 17) & 1;
    uint32_t newBit = bit22 ^ bit17;

    noiseLFSR = ((noiseLFSR << 1) | newBit) & 0x7FFFFF; // Keep it 23 bits

    // Return the top 12 bits as SID does
    return (noiseLFSR >> 11) & 0x0FFF;
}

void Oscillator::updatePhase()
{
    if ((control & 0x02) && syncSource && syncSource->getPhaseOverflow())
    {
        resetPhase();
    }
    phase += frequency / sampleRate;
    phaseOverflow = (phase >= 1.0);
    phase -= floor(phase);
}
