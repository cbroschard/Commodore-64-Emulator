// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include <iomanip>
#include <sstream>
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
    control(0),
    accumulator24(0),
    frequencyReg(0)
{

}

Oscillator::~Oscillator() = default;

double Oscillator::convertToFloat(uint16_t sampleBits)
{
    return applySIDWaveformDac(sampleBits, sidModel_);
}

void Oscillator::setFrequency(uint16_t freqRegValue)
{
    frequencyReg = freqRegValue;

    frequency =
        (static_cast<double>(frequencyReg) * sidClockFrequency) / 16777216.0;
}

void Oscillator::setAccumulator24(uint32_t value)
{
    accumulator24 = value & 0x00FFFFFF;
    phase = static_cast<double>(accumulator24) / 16777216.0;
}

void Oscillator::setPhase(double value)
{
    phase = value - std::floor(value);

    accumulator24 =
        static_cast<uint32_t>(std::clamp(phase, 0.0, 0.999999999) * 16777216.0)
        & 0x00FFFFFF;
}

void Oscillator::setSIDClockFrequency(double clockFrequency)
{
    sidClockFrequency = clockFrequency;

    frequency =
        (static_cast<double>(frequencyReg) * sidClockFrequency) / 16777216.0;
}

void Oscillator::resetPhase()
{
    phase = 0.0;
    accumulator24 = 0;
    phaseOverflow = false;
}

void Oscillator::setControl(uint8_t controlValue)
{
    const bool oldTest = (control & 0x08) != 0;
    const bool newTest = (controlValue & 0x08) != 0;

    if (newTest && !oldTest)
    {
        resetPhase();
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
        const uint16_t noiseBits = getNoiseOutputBits();

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
    updatePhase();
    return outputSample();
}

void Oscillator::reset()
{
    phase = 0.0;
    accumulator24 = 0;
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

void Oscillator::clockNoiseLFSR()
{
    const uint32_t bit22 = (noiseLFSR >> 22) & 1;
    const uint32_t bit17 = (noiseLFSR >> 17) & 1;
    const uint32_t newBit = bit22 ^ bit17;

    noiseLFSR = ((noiseLFSR << 1) | newBit) & 0x7FFFFF;
}

uint16_t Oscillator::getNoiseOutputBits() const
{
    // SID noise output uses selected LFSR bits:
    // output bits from LFSR bits 20, 18, 14, 11, 9, 5, 2, 0.
    const uint8_t noise8 =
        (((noiseLFSR >> 20) & 1) << 7) |
        (((noiseLFSR >> 18) & 1) << 6) |
        (((noiseLFSR >> 14) & 1) << 5) |
        (((noiseLFSR >> 11) & 1) << 4) |
        (((noiseLFSR >>  9) & 1) << 3) |
        (((noiseLFSR >>  5) & 1) << 2) |
        (((noiseLFSR >>  2) & 1) << 1) |
        (((noiseLFSR >>  0) & 1) << 0);

    // Expand 8-bit noise to your existing 12-bit waveform path.
    return static_cast<uint16_t>((noise8 << 4) | (noise8 >> 4));
}

uint16_t Oscillator::getNoiseBits()
{
    return getNoiseOutputBits();
}

void Oscillator::updatePhase()
{
    const double sidCyclesThisSample =
        (sampleRate > 0.0 && sidClockFrequency > 0.0)
            ? (sidClockFrequency / sampleRate)
            : 1.0;

    clock(sidCyclesThisSample);
}

void Oscillator::clock(double sidCycles)
{
    if (sidCycles <= 0.0)
        return;

    if (control & 0x08)
    {
        resetPhase();
        noiseLFSR = 0x7FFFFF;
        return;
    }

    phaseOverflow = false;

    const uint32_t fullCycles =
        static_cast<uint32_t>(std::floor(sidCycles));

    const double fractionalCycles =
        sidCycles - static_cast<double>(fullCycles);

    for (uint32_t i = 0; i < fullCycles; ++i)
    {
        if ((control & 0x02) && syncSource && syncSource->getPhaseOverflow())
        {
            resetPhase();
        }

        const uint32_t oldAcc = accumulator24 & 0x00FFFFFF;
        accumulator24 = (accumulator24 + frequencyReg) & 0x00FFFFFF;

        const bool overflow = accumulator24 < oldAcc;

        if (overflow)
        {
            phaseOverflow = true;

            if (control & 0x80)
                clockNoiseLFSR();
        }
    }

    if (fractionalCycles > 0.0)
    {
        const uint32_t oldAcc = accumulator24 & 0x00FFFFFF;

        const double next =
            static_cast<double>(oldAcc) +
            (static_cast<double>(frequencyReg) * fractionalCycles);

        accumulator24 =
            static_cast<uint32_t>(std::fmod(next, 16777216.0)) & 0x00FFFFFF;

        if (accumulator24 < oldAcc)
        {
            phaseOverflow = true;

            if (control & 0x80)
                clockNoiseLFSR();
        }
    }

    phase =
        static_cast<double>(accumulator24 & 0x00FFFFFF) / 16777216.0;
}

double Oscillator::outputSample()
{
    if ((control & 0xF0) == 0)
        return 0.0;

    uint16_t mixedBits = 0xFFFF;
    bool waveformSelected = false;

    if (control & 0x10) // Triangle
    {
        mixedBits &= getTriangleBits();
        waveformSelected = true;
    }

    if (control & 0x20) // Sawtooth
    {
        if (!waveformSelected)
            mixedBits = getSawBits();
        else
            mixedBits &= getSawBits();

        waveformSelected = true;
    }

    if (control & 0x40) // Pulse
    {
        if (!waveformSelected)
            mixedBits = getPulseBits();
        else
            mixedBits &= getPulseBits();

        waveformSelected = true;
    }

    if (control & 0x80) // Noise
    {
        if (!waveformSelected)
            mixedBits = getNoiseBits();
        else
            mixedBits &= getNoiseBits();

        waveformSelected = true;
    }

    if (!waveformSelected)
        return 0.0;

    return convertToFloat(mixedBits);
}

std::string Oscillator::dumpDebug(uint16_t freqReg, uint16_t pulseWidthReg) const
{
    std::ostringstream out;

    const double phaseWrapped = phase - std::floor(phase);
    const double dutyPercent = pulseWidth * 100.0;

    const bool tri   = (control & 0x10) != 0;
    const bool saw   = (control & 0x20) != 0;
    const bool pulse = (control & 0x40) != 0;
    const bool noise = (control & 0x80) != 0;
    const bool test  = (control & 0x08) != 0;
    const bool ring  = (control & 0x04) != 0;
    const bool sync  = (control & 0x02) != 0;
    const bool gate  = (control & 0x01) != 0;

    out << "OSC Debug:\n";

    out << "  FREQ reg:          $" << std::hex << std::uppercase
        << std::setw(4) << std::setfill('0') << freqReg
        << std::dec << " (" << freqReg << ")\n";

    out << std::fixed << std::setprecision(3);
    out << "  Frequency:         " << frequency << " Hz\n";

    out << "  Phase:             " << std::setprecision(6)
        << phaseWrapped << "\n";

    out << "  Phase overflow:    " << (phaseOverflow ? "Y" : "N") << "\n";

    out << "  Accumulator24:      $" << std::hex << std::uppercase
        << std::setw(6) << std::setfill('0') << (accumulator24 & 0x00FFFFFF)
        << std::dec << "\n";

    out << "  Internal FREQ reg:  $" << std::hex << std::uppercase
        << std::setw(4) << std::setfill('0') << frequencyReg
        << std::dec << " (" << frequencyReg << ")\n";

    out << "  PW reg:            $" << std::hex << std::uppercase
        << std::setw(4) << std::setfill('0') << (pulseWidthReg & 0x0FFF)
        << std::dec << " (" << (pulseWidthReg & 0x0FFF) << "/4095)\n";

    out << std::fixed << std::setprecision(2);
    out << "  Pulse width:       " << dutyPercent << "%\n";

    out << "  CTRL:              $" << std::hex << std::uppercase
        << std::setw(2) << std::setfill('0') << static_cast<int>(control)
        << std::dec << "  ";

    out << "["
        << "GATE="  << (gate  ? "Y" : "N") << " "
        << "SYNC="  << (sync  ? "Y" : "N") << " "
        << "RING="  << (ring  ? "Y" : "N") << " "
        << "TEST="  << (test  ? "Y" : "N") << " "
        << "TRI="   << (tri   ? "Y" : "N") << " "
        << "SAW="   << (saw   ? "Y" : "N") << " "
        << "PULSE=" << (pulse ? "Y" : "N") << " "
        << "NOISE=" << (noise ? "Y" : "N")
        << "]\n";

    out << "  Noise LFSR:        $" << std::hex << std::uppercase
        << std::setw(6) << std::setfill('0') << (noiseLFSR & 0x7FFFFF)
        << std::dec << "\n";

    out << "  Noise output:      $" << std::hex << std::uppercase
        << std::setw(4) << std::setfill('0') << getNoiseOutputBits()
        << std::dec << "\n";

    out << "  OSC3 read value:   $" << std::hex << std::uppercase
        << std::setw(2) << std::setfill('0') << static_cast<int>(readOutput8())
        << std::dec << "\n";

    if (syncSource)
    {
        const double srcPhase =
            syncSource->getPhase() - std::floor(syncSource->getPhase());

        out << std::fixed << std::setprecision(6);
        out << "  SYNC source phase: " << srcPhase
            << " overflow=" << (syncSource->getPhaseOverflow() ? "Y" : "N")
            << "\n";
    }
    else
    {
        out << "  SYNC source phase: (none)\n";
    }

    if (ringSource)
    {
        const double srcPhase =
            ringSource->getPhase() - std::floor(ringSource->getPhase());

        out << std::fixed << std::setprecision(6);
        out << "  RING source phase: " << srcPhase << "\n";
    }
    else
    {
        out << "  RING source phase: (none)\n";
    }

    out << std::fixed << std::setprecision(3);
    out << "  SID clock:         " << sidClockFrequency << " Hz\n";
    out << "  Sample rate:       " << sampleRate << " Hz\n";

    return out.str();
}
