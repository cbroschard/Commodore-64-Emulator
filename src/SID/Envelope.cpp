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

Envelope::Envelope() :
    state(State::Release),
    nextState(State::Release),
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
    statePipeline(0),
    sustainCounter(0),
    rateCounter(0),
    ratePeriod(9),
    resetRateCounter(false),
    holdZero(true)
{

}

Envelope::~Envelope() = default;

void Envelope::trigger()
{
    if (statePipeline > 0 && nextState == State::Release)
        statePipeline = 0;

    nextState = State::Attack;

    //
    // First cycle of gate-on briefly activates Decay/Sustain.
    //
    state = State::DecaySustain;
    ratePeriod = getRatePeriod(decayRate);

    statePipeline = 2;

    if (resetRateCounter || exponentialPipeline == 2)
        envelopePipeline = (exponentialPeriod == 1 || exponentialPipeline == 2) ? 2 : 4;
    else if (exponentialPipeline == 1)
        statePipeline = 3;

    exponentialPipeline = 0;
}

void Envelope::release()
{
    //
    // Gate-off cancels an in-flight attack transition.
    //
    if (statePipeline > 0 && nextState == State::Attack)
        statePipeline = 0;

    nextState = State::Release;
    statePipeline = (envelopePipeline > 0) ? 3 : 2;
}

void Envelope::reset()
{
    state               = State::Release;
    nextState           = State::Release;
    level               = 0.0;

    envCounter          = 0;

    exponentialCounter  = 0;
    exponentialPeriod   = 1;
    exponentialPipeline = 0;

    envelopePipeline    = 0;

    statePipeline       = 0;

    rateCounter         = 0;
    ratePeriod          = 9;
    resetRateCounter    = false;

    holdZero            = true;
}

uint8_t Envelope::readOutput8() const
{
    return envCounter;
}

void Envelope::setEnvelopeCounter(uint8_t value)
{
    envCounter = value;
    syncLevelFromCounter();
}

bool Envelope::isIdle() const
{
    return state == State::Release && holdZero && envCounter == 0;
}

void Envelope::clock(double sidCycles)
{
    if (sidCycles <= 0.0)
        return;

    const uint32_t cycles = static_cast<uint32_t>(std::floor(sidCycles));

    for (uint32_t i = 0; i < cycles; ++i)
    {
        if (statePipeline > 0)
        {
            --statePipeline;

            switch (nextState)
            {
                case State::Attack:
                {
                    if (statePipeline == 0)
                    {
                        state = State::Attack;
                        ratePeriod = getRatePeriod(attackRate);
                        holdZero = false;

                        exponentialCounter = 0;
                    }

                    break;
                }

                case State::Release:
                {
                    if ((state == State::Attack && statePipeline == 0) || (state == State::DecaySustain && statePipeline == 1))
                    {
                        state = State::Release;
                        ratePeriod = getRatePeriod(releaseRate);
                    }

                    break;
                }

                default:
                    break;
            }
        }

        if (envelopePipeline > 0)
        {
            --envelopePipeline;

            if (envelopePipeline == 0)
                stepEnvelopeCounter();
        }

        if (exponentialPipeline > 0)
        {
            --exponentialPipeline;

            if (exponentialPipeline == 0)
            {
                exponentialCounter = 0;

                if ((state == State::DecaySustain && envCounter != sustainCounter) || state == State::Release)
                    envelopePipeline = 1;
            }
        }

        if (resetRateCounter)
        {
            rateCounter = 0;
            resetRateCounter = false;

            switch (state)
            {
                case State::Attack:
                {
                    // First attack envelope step is delayed through
                    // the envelope counter pipeline.
                    exponentialCounter = 0;

                    if (envelopePipeline == 0)
                        envelopePipeline = 2;

                    break;
                }

                case State::DecaySustain:
                case State::Release:
                {
                    if (!holdZero)
                    {
                        ++exponentialCounter;

                        if (exponentialCounter == exponentialPeriod)
                        {
                            exponentialCounter = 0;
                            exponentialPipeline = (exponentialPeriod != 1) ? 2 : 1;
                        }
                    }
                    break;
                }
           }
        }

        //
        // Select the rate period for the currently active state.
        //
        switch (state)
        {
            case State::Attack:
                ratePeriod = getRatePeriod(attackRate);
                break;

            case State::DecaySustain:
                ratePeriod = getRatePeriod(decayRate);
                break;

            case State::Release:
                ratePeriod = getRatePeriod(releaseRate);
                break;
        }

        if (rateCounter != ratePeriod)
            rateCounter = static_cast<uint16_t>((rateCounter + 1) & 0x7FFF);
        else
            resetRateCounter = true;
    }

    syncLevelFromCounter();
}

double Envelope::output() const
{
    return level;
}

void Envelope::setADSR(uint8_t attack, uint8_t decay, uint8_t sustain, uint8_t release)
{
    attackRate      = attack  & 0x0F;
    decayRate       = decay   & 0x0F;
    sustainRate     = sustain & 0x0F;
    releaseRate     = release & 0x0F;

    sustainCounter  = static_cast<uint8_t>((sustainRate << 4) | sustainRate);
    sustainLevel    = static_cast<double>(sustainCounter) / 255.0;

    attackTime      = SID_ATTACK_S[attackRate];
    decayTime       = SID_DECAY_RELEASE_S[decayRate];
    releaseTime     = SID_DECAY_RELEASE_S[releaseRate];
}

std::string Envelope::stateToString(State s)
{
    switch (s)
    {
        case State::Attack:
            return "Attack";

        case State::DecaySustain:
            return "Decay/Sustain";

        case State::Release:
            return "Release";
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

    if (state == State::DecaySustain)
    {
        //
        // Sustain is simply the point where the envelope counter
        // equals the selected sustain level.
        //
        if (envCounter == sustainCounter)
            return;

        envCounter = static_cast<uint8_t>(envCounter - 1);

        if (envCounter == 0)
            holdZero = true;

        updateExponentialPeriod();
        return;
    }

    if (state == State::Release)
    {
        if (envCounter == 0)
        {
            holdZero = true;
            return;
        }

        envCounter = static_cast<uint8_t>(envCounter - 1);

        if (envCounter == 0)
            holdZero = true;

        updateExponentialPeriod();
    }
}

void Envelope::stepEnvelopeCounter()
{
    if (state == State::Attack)
    {
        envCounter =static_cast<uint8_t>(envCounter + 1);

        if (envCounter == 0xFF)
        {
            state = State::DecaySustain;
            nextState = State::DecaySustain;

            exponentialCounter = 0;
            exponentialPeriod = 1;
        }

        return;
    }

    if (state == State::DecaySustain || state == State::Release)
    {
        stepDecayRelease();
        return;
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
    out << "  Envelope pipe:      " << static_cast<int>(envelopePipeline) << "\n";
    out << "  State pipe:         "  << static_cast<int>(statePipeline) << "\n";
    out << "  Next state:         "  << stateToString(nextState) << "\n";
    out << "  Rate counter:       " << rateCounter << "\n";
    out << "  Rate period:        " << ratePeriod << "\n";
    out << "  Rate reset pending:  " << (resetRateCounter ? "Y" : "N") << "\n";
    out << "  Hold zero:          " << (holdZero ? "Y" : "N") << "\n";

    return out.str();
}
