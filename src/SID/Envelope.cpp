// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include "SID/Envelope.h"
#include "SID/SIDEnvelopeTables.h"

Envelope::Envelope(double sampleRate) :
    sidClockFrequency(1022727.0), // NTSC default; SID::setMode will correct it
    sampleRate(sampleRate),
    state(State::Idle),
    level(0.0),
    attackTime(0.1),
    decayTime(0.1),
    sustainLevel(0.7),
    releaseTime(0.2),
    attackRate(0),
    decayRate(0),
    sustainRate(0),
    releaseRate(0),
    envCounter(0),
    exponentialCounter(0),
    exponentialPeriod(1),
    exponentialPipeline(0),
    envelopePipeline(0),
    sustainCounter(0),
    rateCounter(0),
    ratePeriod(9),
    holdZero(true)
{
    setParameters(attackTime, decayTime, sustainLevel, releaseTime);
}

Envelope::~Envelope() = default;

void Envelope::trigger()
{
    state = State::Attack;
    holdZero = false;
    ratePeriod = getRatePeriod(attackRate);

    envelopePipeline = 0;
}

void Envelope::release()
{
    state               = State::Release;
    ratePeriod          = getRatePeriod(releaseRate);
}

void Envelope::reset()
{
    state               = State::Idle;
    level               = 0.0;

    envCounter          = 0;

    exponentialCounter  = 0;
    exponentialPeriod   = 1;
    exponentialPipeline = 0;

    envelopePipeline    = 0;

    rateCounter         = 0;
    ratePeriod          = 9;

    holdZero            = true;
}

void Envelope::setSIDClockFrequency(double frequency)
{
    sidClockFrequency = frequency;

    // Recompute ADSR periods using current ADSR nibbles and new clock.
    setADSR(attackRate, decayRate, sustainRate, releaseRate);
}

uint8_t Envelope::readOutput8() const
{
    return envCounter;
}

void Envelope::setLevel(double newLevel)
{
    level = std::clamp(newLevel, 0.0, 1.0);
    envCounter = static_cast<uint8_t>(std::round(level * 255.0));
}

double Envelope::processSample()
{
    const double sidCyclesThisSample =
        (sampleRate > 0.0) ? (sidClockFrequency / sampleRate) : 1.0;

    clock(sidCyclesThisSample);
    return output();
}

bool Envelope::isIdle() const
{
    return state == State::Idle;
}

void Envelope::clock(double sidCycles)
{
    if (sidCycles <= 0.0)
        return;

    const uint32_t cycles = static_cast<uint32_t>(std::floor(sidCycles));

    for (uint32_t i = 0; i < cycles; ++i)
    {
        // Complete a pending envelope-counter update.
        if (envelopePipeline > 0)
        {
            --envelopePipeline;

            if (envelopePipeline == 0)
                stepDecayRelease();
        }

        // Exponential divider completion schedules the
        // envelope update for the following cycle.
        if (exponentialPipeline > 0)
        {
            --exponentialPipeline;

            if (exponentialPipeline == 0)
                envelopePipeline = 1;
        }

        switch (state)
        {
            case State::Attack:
                ratePeriod = getRatePeriod(attackRate);
                break;

            case State::Decay:
            case State::Sustain:
                ratePeriod = getRatePeriod(decayRate);
                break;

            case State::Release:
            case State::Idle:
                ratePeriod = getRatePeriod(releaseRate);
                break;
        }

        // SID rate counter is 15-bit.
        rateCounter =
            static_cast<uint16_t>((rateCounter + 1) & 0x7FFF);

        // Equality is intentional. Do NOT use >=.
        if (rateCounter != ratePeriod)
            continue;

        rateCounter = 0;

        switch (state)
        {
            case State::Idle:
            {
                break;
            }

            case State::Attack:
            {
                envCounter = static_cast<uint8_t>(envCounter + 1);

                if (envCounter == 0xFF)
                {
                    state = State::Decay;

                    exponentialCounter = 0;
                    updateExponentialPeriod();
                }

                break;
            }

            case State::Decay:
            {
                if (exponentialPipeline == 0)
                {
                    ++exponentialCounter;

                    if (exponentialCounter >= exponentialPeriod)
                    {
                        exponentialCounter = 0;
                        exponentialPipeline = 1;
                    }
                }

                break;
            }

            case State::Sustain:
            {
                // Hold current envelope level.
                break;
            }

            case State::Release:
            {
                if (envCounter == 0)
                {
                    holdZero = true;
                    state = State::Idle;
                    exponentialCounter = 0;
                    exponentialPeriod = 1;
                    exponentialPipeline = 0;
                    break;
                }

                if (exponentialPipeline == 0)
                {
                    ++exponentialCounter;

                    if (exponentialCounter >= exponentialPeriod)
                    {
                        exponentialCounter = 0;
                        exponentialPipeline = 1;
                    }
                }

                break;
            }
        }
    }

    syncLevelFromCounter();
}

double Envelope::output() const
{
    return level;
}

void Envelope::setSampleRate(double sample)
{
    sampleRate = sample;
}

void Envelope::setParameters(double attack, double decay, double sustain, double release)
{
    attackTime = attack;
    decayTime = decay;
    sustainLevel = std::clamp(sustain, 0.0, 1.0);
    releaseTime = release;

    sustainCounter = static_cast<uint8_t>(std::round(sustainLevel * 255.0));
}

void Envelope::setADSR(uint8_t attack, uint8_t decay, uint8_t sustain, uint8_t release)
{
    attackRate  = attack  & 0x0F;
    decayRate   = decay   & 0x0F;
    sustainRate = sustain & 0x0F;
    releaseRate = release & 0x0F;

    sustainLevel = static_cast<double>(sustainRate) / 15.0;

    sustainCounter = static_cast<uint8_t>(std::round(sustainLevel * 255.0));

    attackTime  = SID_ATTACK_S[attackRate];
    decayTime   = SID_DECAY_RELEASE_S[decayRate];
    releaseTime = SID_DECAY_RELEASE_S[releaseRate];
}

std::string Envelope::stateToString(State s) {
    switch (s)
    {
        case State::Idle:    return "Idle";
        case State::Attack:  return "Attack";
        case State::Decay:   return "Decay";
        case State::Sustain: return "Sustain";
        case State::Release: return "Release";
    }
    return "Unknown";
}

void Envelope::syncLevelFromCounter()
{
    level = static_cast<double>(envCounter) / 255.0;
}

void Envelope::updateExponentialPeriod()
{
    switch (envCounter)
    {
        case 0xFF:
            exponentialPeriod = 1;
            break;

        case 0x5D:
            exponentialPeriod = 2;
            break;

        case 0x36:
            exponentialPeriod = 4;
            break;

        case 0x1A:
            exponentialPeriod = 8;
            break;

        case 0x0E:
            exponentialPeriod = 16;
            break;

        case 0x06:
            exponentialPeriod = 30;
            break;

        case 0x00:
            exponentialPeriod = 1;
            break;

        default:
            // Important: retain the current period.
            break;
    }
}

uint16_t Envelope::getRatePeriod(uint8_t rate) const
{
    static constexpr uint16_t periods[16] =
    {
        9,
        32,
        63,
        95,
        149,
        220,
        267,
        313,
        392,
        977,
        1954,
        3126,
        3907,
        11720,
        19532,
        31251
    };

    return periods[rate & 0x0F];
}

void Envelope::stepDecayRelease()
{
    if (holdZero)
        return;

    if (state == State::Decay)
    {
        if (envCounter <= sustainCounter)
        {
            state = State::Sustain;
            return;
        }

        envCounter = static_cast<uint8_t>(envCounter - 1);

        if (envCounter == 0)
            holdZero = true;

        updateExponentialPeriod();

        if (envCounter <= sustainCounter)
        {
            envCounter = sustainCounter;
            state = State::Sustain;
        }

        return;
    }

    if (state == State::Release)
    {
        if (envCounter == 0)
        {
            holdZero = true;
            state = State::Idle;
            return;
        }

        envCounter = static_cast<uint8_t>(envCounter - 1);

        if (envCounter == 0)
        {
            holdZero = true;
            state = State::Idle;
        }

        updateExponentialPeriod();
    }
}

std::string Envelope::dumpDebug() const
{
    std::ostringstream out;

    out << "ENV Debug:\n";

    out << "  State:              " << stateToString(state) << "\n";

    out << "  ADSR nibbles:       "
        << "A=$" << std::hex << std::uppercase << static_cast<int>(attackRate)
        << " D=$" << static_cast<int>(decayRate)
        << " S=$" << static_cast<int>(sustainRate)
        << " R=$" << static_cast<int>(releaseRate)
        << std::dec << "\n";

    out << "  Counter:            $" << std::hex << std::uppercase
        << std::setw(2) << std::setfill('0')
        << static_cast<int>(envCounter)
        << std::dec << std::setfill(' ')
        << " (" << static_cast<int>(envCounter) << "/255)\n";

    out << std::fixed << std::setprecision(6);
    out << "  Level:              " << level << "\n";

    out << std::setprecision(3);
    out << "  Attack time:        " << attackTime << " s\n";
    out << "  Decay time:         " << decayTime << " s\n";
    out << "  Sustain level:      " << sustainLevel << "\n";
    out << "  Release time:       " << releaseTime << " s\n";

    out << "  Sustain counter:    $" << std::hex << std::uppercase
        << std::setw(2) << std::setfill('0')
        << static_cast<int>(sustainCounter)
        << std::dec << std::setfill(' ')
        << " (" << static_cast<int>(sustainCounter) << "/255)\n";

    out << std::fixed << std::setprecision(3);
    out << "  Exponential count:  " << exponentialCounter << "\n";
    out << "  Exponential period: " << exponentialPeriod << "\n";
    out << "  Exponential pipe:   " << static_cast<int>(exponentialPipeline) << "\n";
    out << "  Rate counter:       " << rateCounter << "\n";
    out << "  Rate period:        " << ratePeriod << "\n";
    out << "  Hold zero:          " << (holdZero ? "Y" : "N") << "\n";

    out << "  SID clock:          " << sidClockFrequency << " Hz\n";
    out << "  Sample rate:        " << sampleRate << " Hz\n";

    const double cyclesPerSample =
        (sampleRate > 0.0) ? (sidClockFrequency / sampleRate) : 0.0;

    out << "  SID cycles/sample:  " << cyclesPerSample << "\n";

    return out.str();
}
