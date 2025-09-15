// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "SID/Voice.h"

Voice::Voice(double sampleRate) :
    osc(sampleRate),
    env(sampleRate),
    filterRouted(false),
    sidClockFrequency(0.0)
{

}

Voice::~Voice() = default;

const Oscillator& Voice::getOscillator() const
{
    return osc;
}

Oscillator& Voice::getOscillator()
{
    return osc;
}

const Envelope& Voice::getEnvelope() const
{
    return env;
}

Envelope& Voice::getEnvelope()
{
    return env;
}

void Voice::trigger()
{
    env.trigger();
}

void Voice::release()
{
    env.release();
}

void Voice::setSIDClockFrequency(double frequency)
{
    sidClockFrequency = frequency;

    // Update oscillator and envelope
    osc.setSIDClockFrequency(sidClockFrequency);
}

void Voice::setFrequency(uint16_t freqValue)
{
    osc.setFrequency(freqValue);
}

void Voice::setPulseWidth(uint16_t pulseWidth)
{
    double duty = static_cast<double>(pulseWidth & 0x0FFF) / 4095.0;
    osc.setPulseWidth(duty);
}

void Voice::setEnvelopeParameters(double attack, double delay, double sustain, double release)
{
    env.setParameters(attack, delay, sustain, release);
}

void Voice::setControl(uint8_t controlValue)
{
    osc.setControl(controlValue);
}

void Voice::setFilterRouted(bool routed)
{
    filterRouted = routed;
}

double Voice::generateVoiceSample()
{
    uint8_t ctrl = osc.getControl();

    // If we're supposed to be filter-routed but the gate is off and
    // the envelope has gone idle, suppress any low-frequency rumble:
    if (filterRouted && env.isIdle() && !(ctrl & 0x01)) {
        return 0.0;
    }

    // Test-bit silence
    if (ctrl & 0x08) {
        env.processSample();
        return 0.0;
    }

    // No waveform selected
    if (!(ctrl & 0xF0)) {
        env.processSample();
        return 0.0;
    }

    double oscSample = osc.generateMixedSample();
    double envLevel  = env.processSample();
    double out       = oscSample * envLevel;

    return (std::abs(out) < 0.001) ? 0.0 : out;
}

void Voice::reset()
{
    osc.setControl(0x00);   // No waveform, gate off, test off
    osc.reset();            // Reset phase, noise state
    env.reset();            // Reset ADSR to idle, level = 0
}
