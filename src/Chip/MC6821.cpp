// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Chip/MC6821.h"

MC6821::MC6821() :
    registers{},
    externalPinsA(0xFF),
    externalPinsB(0xFF),
    ca1(false),
    ca2(false),
    cb1(false),
    cb2(false),
    irqA1Flag(false),
    irqA2Flag(false),
    irqB1Flag(false),
    irqB2Flag(false)
{

}

MC6821::~MC6821() = default;

void MC6821::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("6821");
    wrtr.writeU32(1); // Version

    wrtr.endChunk();
}

bool MC6821::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "6821", 4) != 0)
        return false;

    return true;
}

void MC6821::reset()
{
    registers       = Registers{};

    externalPinsA   = 0xFF;
    externalPinsB   = 0xFF;

    ca1             = false;
    ca2             = false;
    cb1             = false;
    cb2             = false;

    irqA1Flag       = false;
    irqA2Flag       = false;
    irqB1Flag       = false;
    irqB2Flag       = false;

    updateIRQA();
    updateIRQB();
}

uint8_t MC6821::read(uint8_t rs)
{
    uint8_t value = 0xFF;

    switch (rs & 0x03)
    {
        case 0:
        {
            value = (registers.cra & 0x04) ? readPortA() : registers.ddra;
            break;
        }

        case 1:
            value = registers.cra;
            break;

        case 2:
        {
            value = (registers.crb & 0x04) ? readPortB() : registers.ddrb;
            break;
        }

        case 3:
            value = registers.crb;
            break;

        default:
            break;
    }

    return value;
}

void MC6821::write(uint8_t rs, uint8_t value)
{
    switch (rs & 0x03)
    {
        case 0:
        case 1:
        case 2:
        case 3:
        default:
            break;
    }
}

uint8_t MC6821::peek(uint8_t rs)
{
    uint8_t value = 0xFF;

    switch (rs & 0x03)
    {
        case 0:
            value = (registers.cra & 0x04) ? peekPortA() : registers.ddra;
            break;

        case 1:
            value = registers.cra;
            break;

        case 2:
            value = (registers.crb & 0x04) ? peekPortB() : registers.ddrb;
            break;

        case 3:
            value = registers.crb;
            break;

        default:
            break;
    }

    return value;
}

uint8_t MC6821::readPortA()
{
    return (registers.ora & registers.ddra) | (externalPinsA & static_cast<uint8_t>(~registers.ddra));
}

uint8_t MC6821::readPortB()
{
    return (registers.orb & registers.ddrb) | (externalPinsB & static_cast<uint8_t>(~registers.ddrb));
}

uint8_t MC6821::peekPortA()
{
    return (registers.ora & registers.ddra) | (externalPinsA & static_cast<uint8_t>(~registers.ddra));
}

uint8_t MC6821::peekPortB()
{
    return (registers.orb & registers.ddrb) | (externalPinsB & static_cast<uint8_t>(~registers.ddrb));
}

uint8_t MC6821::readCRA() const
{

}

uint8_t MC6821::readCRB() const
{

}

void MC6821::writePortA(uint8_t value)
{

}

void MC6821::writePortB(uint8_t value)
{

}

void MC6821::writeCRA(uint8_t value)
{

}

void MC6821::writeCRB(uint8_t value)
{

}

void  MC6821::updateIRQA()
{

}

void MC6821::updateIRQB()
{

}
