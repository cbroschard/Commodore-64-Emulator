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
    level(0.0),
    attackRate(0),
    decayRate(0),
    sustainRate(0),
    releaseRate(0),
    envCounter(0),
    exponentialCounter(0),
    exponentialPeriod(1),
    sustainCounter(0),
    rateCounter(0),
    holdZero(true)
{

}

Envelope::~Envelope() = default;

void Envelope::trigger()
{
    state = State::Attack;
    holdZero = false;
}

void Envelope::release()
{
    state = State::Release;
}

void Envelope::reset()
{
    state               = State::Release;
    level               = 0.0;

    envCounter          = 0;

    exponentialCounter  = 0;
    exponentialPeriod   = 1;

    rateCounter         = 0;

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
        //
        // Select the rate period for the current envelope state.
        //
        uint16_t currentRatePeriod = 0;

        switch (state)
        {
            case State::Attack:
                currentRatePeriod = getRatePeriod(attackRate);
                break;

            case State::DecaySustain:
                currentRatePeriod = getRatePeriod(decayRate);
                break;

            case State::Release:
                currentRatePeriod = getRatePeriod(releaseRate);
                break;
        }

        //
        // SID envelope rate counter.
        //
        // The counter is effectively 15-bit and is not reset when
        // the ADSR rate changes. On overflow, the SID behavior skips
        // $0000, which is part of the classic ADSR delay quirk.
        //
        ++rateCounter;

        if (rateCounter & 0x8000)
        {
            rateCounter = static_cast<uint16_t>((rateCounter + 1) & 0x7FFF);
        }

        if (rateCounter != currentRatePeriod)
            continue;

        rateCounter = 0;

        switch (state)
        {
            case State::Attack:
            {
                //
                // Attack is linear. The first actual Attack step resets
                // the exponential divider.
                //
                exponentialCounter = 0;

                envCounter =
                    static_cast<uint8_t>(envCounter + 1);

                if (envCounter == 0xFF)
                    state = State::DecaySustain;

                updateExponentialPeriod();

                if (envCounter == 0x00)
                    holdZero = true;

                break;
            }

            case State::DecaySustain:
            {
                if (holdZero || envCounter == sustainCounter)
                    break;

                ++exponentialCounter;

                if (exponentialCounter == exponentialPeriod)
                {
                    exponentialCounter = 0;

                    if (envCounter > 0)
                        --envCounter;

                    updateExponentialPeriod();

                    if (envCounter == 0)
                        holdZero = true;
                }

                break;
            }

            case State::Release:
            {
                ++exponentialCounter;

                if (exponentialCounter == exponentialPeriod)
                {
                    exponentialCounter = 0;

                    envCounter =
                        static_cast<uint8_t>(envCounter - 1);

                    updateExponentialPeriod();

                    if (envCounter == 0x00)
                        holdZero = true;
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

void Envelope::setADSR(uint8_t attack, uint8_t decay, uint8_t sustain, uint8_t release)
{
    attackRate  = attack  & 0x0F;
    decayRate   = decay   & 0x0F;
    sustainRate = sustain & 0x0F;
    releaseRate = release & 0x0F;

    //
    // SID sustain level is the 4-bit sustain nibble
    // replicated into both halves of the 8-bit envelope value.
    //
    sustainCounter = static_cast<uint8_t>((sustainRate << 4) | sustainRate);
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
    out << "  Attack time:        "
        << SID_ATTACK_S[attackRate] << " s\n";

    out << "  Decay time:         "
        << SID_DECAY_RELEASE_S[decayRate] << " s\n";

    out << "  Sustain level:      "
        << (static_cast<double>(sustainCounter) / 255.0) << "\n";

    out << "  Release time:       "
        << SID_DECAY_RELEASE_S[releaseRate] << " s\n";

    out << "  Sustain counter:    $" << std::hex << std::uppercase
        << std::setw(2) << std::setfill('0')
        << static_cast<int>(sustainCounter)
        << std::dec << std::setfill(' ')
        << " (" << static_cast<int>(sustainCounter) << "/255)\n";

    out << std::fixed << std::setprecision(3);
    out << "  Exponential count:  " << exponentialCounter << "\n";
    out << "  Exponential period: " << exponentialPeriod << "\n";
    out << "  Rate counter:       " << rateCounter << "\n";

    uint16_t currentRatePeriod = 0;

    switch (state)
    {
        case State::Attack:
            currentRatePeriod = getRatePeriod(attackRate);
            break;

        case State::DecaySustain:
            currentRatePeriod = getRatePeriod(decayRate);
            break;

        case State::Release:
            currentRatePeriod = getRatePeriod(releaseRate);
            break;
    }

    out << "  Rate period:        " << currentRatePeriod << "\n";
    out << "  Hold zero:          " << (holdZero ? "Y" : "N") << "\n";

    return out.str();
}
