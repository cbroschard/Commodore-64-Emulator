// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include <algorithm>
#include <cmath>
#include "SID/Envelope.h"

Envelope::Envelope(double sampleRate) :
    sidClockFrequency(1022727.0), // NTSC default; SID::setMode will correct it
    sampleRate(sampleRate),
    state(State::Idle),
    level(0.0),
    attackTime(0.1),
    decayTime(0.1),
    sustainLevel(0.7),
    releaseTime(0.2),
    envCounter(0),
    stepAccumulator(0.0),
    attackStepCycles(1.0),
    decayStepCycles(1.0),
    releaseStepCycles(1.0),
    sustainCounter(0)
{
    setParameters(attackTime, decayTime, sustainLevel, releaseTime);
}

Envelope::~Envelope() = default;

void Envelope::trigger()
{
    state = State::Attack;
    stepAccumulator = 0.0;
}

void Envelope::release()
{
    state = State::Release;
    stepAccumulator = 0.0;
}

void Envelope::reset()
{
    state = State::Idle;
    level = 0.0;
    envCounter = 0;
    stepAccumulator = 0.0;
}

void Envelope::setSIDClockFrequency(double frequency)
{
    sidClockFrequency = frequency;
    setParameters(attackTime, decayTime, sustainLevel, releaseTime);
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
    const double sidCyclesThisSample = (sampleRate > 0.0) ? (sidClockFrequency / sampleRate) : 1.0;

    switch (state)
    {
        case State::Idle:
        {
            envCounter = 0;
            syncLevelFromCounter();
            break;
        }

        case State::Attack:
        {
            stepAccumulator += sidCyclesThisSample;

            while (stepAccumulator >= attackStepCycles)
            {
                stepAccumulator -= attackStepCycles;

                if (envCounter < 0xFF)
                {
                    ++envCounter;

                    if (envCounter == 0xFF)
                    {
                        state = State::Decay;
                        stepAccumulator = 0.0;
                        break;
                    }
                }
                else
                {
                    envCounter = 0xFF;
                    state = State::Decay;
                    stepAccumulator = 0.0;
                    break;
                }
            }

            syncLevelFromCounter();
            break;
        }

        case State::Decay:
        {
            stepAccumulator += sidCyclesThisSample;

            while (stepAccumulator >= decayStepCycles)
            {
                stepAccumulator -= decayStepCycles;

                if (envCounter > sustainCounter)
                {
                    --envCounter;
                }
                else
                {
                    envCounter = sustainCounter;
                    state = State::Sustain;
                    stepAccumulator = 0.0;
                    break;
                }
            }

            syncLevelFromCounter();
            break;
        }

        case State::Sustain:
        {
            envCounter = sustainCounter;
            syncLevelFromCounter();
            break;
        }

        case State::Release:
        {
            stepAccumulator += sidCyclesThisSample;

            while (stepAccumulator >= releaseStepCycles)
            {
                stepAccumulator -= releaseStepCycles;

                if (envCounter > 0)
                {
                    --envCounter;

                    if (envCounter == 0)
                    {
                        state = State::Idle;
                        stepAccumulator = 0.0;
                        break;
                    }
                }
                else
                {
                    envCounter = 0;
                    state = State::Idle;
                    stepAccumulator = 0.0;
                    break;
                }
            }

            syncLevelFromCounter();
            break;
        }
    }

    return level;
}

bool Envelope::isIdle() const
{
    return state == State::Idle;
}

void Envelope::setSampleRate(double sample)
{
    sampleRate = sample;
    setParameters(attackTime, decayTime, sustainLevel, releaseTime);
}

void Envelope::setParameters(double attack, double decay, double sustain, double release)
{
    attackTime = attack;
    decayTime = decay;
    sustainLevel = std::clamp(sustain, 0.0, 1.0);
    releaseTime = release;

    sustainCounter = static_cast<uint8_t>(std::round(sustainLevel * 255.0));

    // Convert ADSR times into SID-cycle intervals per 8-bit envelope step.
    // This keeps timing tied to the SID clock instead of directly to audio sample count.
    attackStepCycles  = std::max(1.0, (attackTime  * sidClockFrequency) / 255.0);
    decayStepCycles   = std::max(1.0, (decayTime   * sidClockFrequency) / 255.0);
    releaseStepCycles = std::max(1.0, (releaseTime * sidClockFrequency) / 255.0);
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
