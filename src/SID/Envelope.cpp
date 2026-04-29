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
    sampleRate(sampleRate),
    state(State::Idle),
    level(0.0),
    attackTime(0.1),
    decayTime(0.1),
    sustainLevel(0.7),
    releaseTime(0.2),
    envCounter(0),
    stepAccumulator(0.0),
    attackStepSamples(1.0),
    decayStepSamples(1.0),
    releaseStepSamples(1.0),
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
            stepAccumulator += 1.0;

            while (stepAccumulator >= attackStepSamples)
            {
                stepAccumulator -= attackStepSamples;

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
            stepAccumulator += 1.0;

            while (stepAccumulator >= decayStepSamples)
            {
                stepAccumulator -= decayStepSamples;

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
            stepAccumulator += 1.0;

            while (stepAccumulator >= releaseStepSamples)
            {
                stepAccumulator -= releaseStepSamples;

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

    // Convert total ADSR times into approximate samples per 8-bit envelope step.
    // This keeps your existing SID_ATTACK_S and SID_DECAY_RELEASE_S tables,
    // but makes the output behave like a stepped 8-bit SID envelope.
    attackStepSamples  = std::max(1.0, (attackTime  * sampleRate) / 255.0);
    decayStepSamples   = std::max(1.0, (decayTime   * sampleRate) / 255.0);
    releaseStepSamples = std::max(1.0, (releaseTime * sampleRate) / 255.0);
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
