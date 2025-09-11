// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "SID/Envelope.h"

Envelope::Envelope(double sampleRate) :
    sampleRate(sampleRate),
    state(State::Idle),
    level(0.0),
    attackTime(0.1),
    decayTime(0.1),
    sustainLevel(0.7),
    releaseTime(0.2)
{
    setParameters(attackTime, decayTime, sustainLevel, releaseTime);
}

Envelope::~Envelope() = default;

void Envelope::trigger()
{
    state = State::Attack;
}

void Envelope::release()
{
    state = State::Release;
}

double Envelope::processSample()
{
    switch(state)
    {
        case State::Idle:
        {
            // No sound is produced, break
            break;
        }
        case State::Attack:
        {
            level += (1.0 - level) * attackCoeff;
            if (level >= 0.999)
            {
                level = 1.0;
                state = State::Decay;
            }
            break;
        }
        case State::Decay:
        {
            level += (sustainLevel - level) * decayCoeff;
            if (level <= sustainLevel + 1e-5)
            {
                level = sustainLevel;
                state = State::Sustain;
            }
            break;
        }
        case State::Sustain:
        {
            // Sustain: maintain the sustain level.
            level = sustainLevel;
            break;
        }
        case State::Release:
        {
            level *= (1.0 - releaseCoeff);
            if (level <= 1e-5)
            {
                level = 0.0;
                state = State::Idle;
            }
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
    sustainLevel = sustain;
    releaseTime = release;

    // precompute coefficients
    attackCoeff  = 1.0 - std::exp(-1.0 / (attackTime  * sampleRate));
    decayCoeff   = 1.0 - std::exp(-1.0 / (decayTime   * sampleRate));
    releaseCoeff = 1.0 - std::exp(-1.0 / (releaseTime * sampleRate));
}

double Envelope::getLevel() const
{
    return level;
}

void Envelope::reset()
{
    state = State::Idle;
    level = 0.0;
}
