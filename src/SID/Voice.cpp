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

void Voice::clockEnvelope(double sidCycles)
{
    env.clock(sidCycles);
}

void Voice::setSIDClockFrequency(double frequency)
{
    sidClockFrequency = frequency;

    osc.setSIDClockFrequency(sidClockFrequency);
    env.setSIDClockFrequency(sidClockFrequency);
}

void Voice::setFrequency(uint16_t freqValue)
{
    osc.setFrequency(freqValue);
}

void Voice::setPulseWidth(uint16_t pulseWidth)
{
    double duty = static_cast<double>(pulseWidth & 0x0FFF) / 4096.0;
    osc.setPulseWidth(duty);
}

void Voice::setEnvelopeParameters(double attack, double delay, double sustain, double release)
{
    env.setParameters(attack, delay, sustain, release);
}

void Voice::setADSR(uint8_t attack, uint8_t decay, uint8_t sustain, uint8_t release)
{
    env.setADSR(attack, decay, sustain, release);
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
    const uint8_t ctrl = osc.getControl();

    // TEST bit forces oscillator silence.
    // Oscillator timing is handled separately by clockOscillator().
    if (ctrl & 0x08)
        return 0.0;

    // No waveform selected: oscillator still free-runs,
    // but audio output is silent.
    if (!(ctrl & 0xF0))
        return 0.0;

    // If routed through the filter and fully idle, output can be silent,
    // but oscillator timing has already advanced separately.
    if (filterRouted && env.isIdle() && !(ctrl & 0x01))
        return 0.0;

    const double oscSample = osc.outputSample();
    const double envLevel  = env.output();

    return oscSample * envLevel;
}

void Voice::reset()
{
    osc.setControl(0x00);   // No waveform, gate off, test off
    osc.reset();            // Reset phase, noise state
    env.reset();            // Reset ADSR to idle, level = 0
}
