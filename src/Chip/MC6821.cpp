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
    irqB2Flag(false),
    irqA(false),
    irqB(false),
    resetLine(true)
{

}

MC6821::~MC6821() = default;

void MC6821::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("6821");
    wrtr.writeU32(1); // Version

    wrtr.writeU8(registers.ora);
    wrtr.writeU8(registers.orb);
    wrtr.writeU8(registers.ddra);
    wrtr.writeU8(registers.ddrb);
    wrtr.writeU8(registers.cra);
    wrtr.writeU8(registers.crb);

    wrtr.writeU8(externalPinsA);
    wrtr.writeU8(externalPinsB);

    wrtr.writeBool(ca1);
    wrtr.writeBool(ca2);
    wrtr.writeBool(cb1);
    wrtr.writeBool(cb2);

    wrtr.writeBool(irqA1Flag);
    wrtr.writeBool(irqA2Flag);
    wrtr.writeBool(irqB1Flag);
    wrtr.writeBool(irqB2Flag);

    wrtr.writeBool(resetLine);

    wrtr.endChunk();
}

bool MC6821::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "6821", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    uint32_t ver = 0;
    if (!rdr.readU32(ver))              { rdr.exitChunkPayload(chunk); return false; }
    if (ver != 1)                       { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readU8(registers.ora))     { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readU8(registers.orb))     { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readU8(registers.ddra))    { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readU8(registers.ddrb))    { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readU8(registers.cra))     { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readU8(registers.crb))     { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readU8(externalPinsA))     { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readU8(externalPinsB))     { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readBool(ca1))             { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(ca2))             { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(cb1))             { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(cb2))             { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readBool(irqA1Flag))       { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(irqA2Flag))       { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(irqB1Flag))       { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(irqB2Flag))       { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readBool(resetLine))       { rdr.exitChunkPayload(chunk); return false; }

    // Normalize post load
    registers.cra &= 0x3F;
    registers.crb &= 0x3F;

    updateIRQA();
    updateIRQB();

    rdr.exitChunkPayload(chunk);
    return true;
}

void MC6821::reset()
{
    registers       = Registers{};

    ca1             = false;
    ca2             = false;
    cb1             = false;
    cb2             = false;

    irqA1Flag       = false;
    irqA2Flag       = false;
    irqB1Flag       = false;
    irqB2Flag       = false;

    irqA            = false;
    irqB            = false;

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
            value = readCRA();
            break;

        case 2:
        {
            value = (registers.crb & 0x04) ? readPortB() : registers.ddrb;
            break;
        }

        case 3:
            value = readCRB();
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
        {
            if (registers.cra & 0x04)
                writePortA(value);
            else
                registers.ddra = value;

            break;
        }

        case 1:
            writeCRA(value);
            break;

        case 2:
        {
            if (registers.crb & 0x04)
                writePortB(value);
            else
                registers.ddrb = value;

            break;
        }

        case 3:
            writeCRB(value);
            break;

        default:
            break;
    }
}

uint8_t MC6821::peek(uint8_t rs) const
{
    uint8_t value = 0xFF;

    switch (rs & 0x03)
    {
        case 0:
            value = (registers.cra & 0x04) ? peekPortA() : registers.ddra;
            break;

        case 1:
            value = readCRA();
            break;

        case 2:
            value = (registers.crb & 0x04) ? peekPortB() : registers.ddrb;
            break;

        case 3:
            value = readCRB();
            break;

        default:
            break;
    }

    return value;
}

uint8_t MC6821::readPortA()
{
    const uint8_t value = (registers.ora & registers.ddra) | (externalPinsA & static_cast<uint8_t>(~registers.ddra));

    irqA1Flag = false;
    irqA2Flag = false;

    updateIRQA();

    return value;
}

uint8_t MC6821::readPortB()
{
    const uint8_t value = (registers.orb & registers.ddrb) |(externalPinsB & static_cast<uint8_t>(~registers.ddrb));

    irqB1Flag = false;
    irqB2Flag = false;

    updateIRQB();

    return value;
}

uint8_t MC6821::peekPortA() const
{
    return (registers.ora & registers.ddra) | (externalPinsA & static_cast<uint8_t>(~registers.ddra));
}

uint8_t MC6821::peekPortB() const
{
    return (registers.orb & registers.ddrb) | (externalPinsB & static_cast<uint8_t>(~registers.ddrb));
}

uint8_t MC6821::readCRA() const
{
    uint8_t value = registers.cra & 0x3F;

    if (irqA2Flag)
        value |= 0x40;

    if (irqA1Flag)
        value |= 0x80;

    return value;
}

uint8_t MC6821::readCRB() const
{
    uint8_t value = registers.crb & 0x3F;

    if (irqB2Flag)
        value |= 0x40;

    if (irqB1Flag)
        value |= 0x80;

    return value;
}

void MC6821::writePortA(uint8_t value)
{
    registers.ora = value;
}

void MC6821::writePortB(uint8_t value)
{
    registers.orb = value;
}

void MC6821::writeCRA(uint8_t value)
{
    registers.cra = value & 0x3F;

    const auto mode = static_cast<C2Mode>((registers.cra >> 3) & 0x07);

    if (static_cast<uint8_t>(mode) >= 4)
        irqA2Flag = false;

    switch (mode)
    {
        case C2Mode::InputFallingNoIRQ:
            break;

        case C2Mode::InputFallingIRQ:
            break;

        case C2Mode::InputRisingNoIRQ:
            break;

        case C2Mode::InputRisingIRQ:
            break;

        case C2Mode::Handshake:
            break;

        case C2Mode::Pulse:
            break;

        case C2Mode::ForceLow:
            ca2 = false;
            break;

        case C2Mode::ForceHigh:
            // output forced high
            ca2 = true;
            break;
    }

    updateIRQA();
}

void MC6821::writeCRB(uint8_t value)
{
    registers.crb = value & 0x3F;

    const auto mode = static_cast<C2Mode>((registers.crb >> 3) & 0x07);

    if (static_cast<uint8_t>(mode) >= 4)
        irqB2Flag = false;

    switch (mode)
    {
        case C2Mode::InputFallingNoIRQ:
            break;

        case C2Mode::InputFallingIRQ:
            break;

        case C2Mode::InputRisingNoIRQ:
            break;

        case C2Mode::InputRisingIRQ:
            break;

        case C2Mode::Handshake:
            break;

        case C2Mode::Pulse:
            break;

        case C2Mode::ForceLow:
            cb2 = false;
            break;

        case C2Mode::ForceHigh:
            cb2 = true;
            break;
    }

    updateIRQB();
}

void MC6821::setCA1(bool level)
{
    const bool rising  = !ca1 && level;
    const bool falling = ca1 && !level;

    ca1 = level;

    const bool risingSelected = (registers.cra & 0x02) != 0;

    if ((risingSelected && rising) || (!risingSelected && falling))
    {
        irqA1Flag = true;
        updateIRQA();
    }
}

void MC6821::setCA2(bool level)
{
    if (registers.cra & 0x20)
        return;

    const bool rising  = !ca2 && level;
    const bool falling = ca2 && !level;

    ca2 = level;

    const bool risingSelected = (registers.cra & 0x10) != 0;

    if ((risingSelected && rising) || (!risingSelected && falling))
    {
        irqA2Flag = true;
        updateIRQA();
    }
}

void MC6821::setCB1(bool level)
{
    const bool rising  = !cb1 && level;
    const bool falling = cb1 && !level;

    cb1 = level;

    const bool risingSelected = (registers.crb & 0x02) != 0;

    if ((risingSelected && rising) || (!risingSelected && falling))
    {
        irqB1Flag = true;
        updateIRQB();
    }
}

void MC6821::setCB2(bool level)
{
    if (registers.crb & 0x20)
        return;

    const bool rising  = !cb2 && level;
    const bool falling = cb2 && !level;

    cb2 = level;

    const bool risingSelected = (registers.crb & 0x10) != 0;

    if ((risingSelected && rising) || (!risingSelected && falling))
    {
        irqB2Flag = true;
        updateIRQB();
    }
}

void MC6821::updateIRQA()
{
    const bool irq1 = irqA1Flag && ((registers.cra & 0x01) != 0);

    const bool ca2IsInput = (registers.cra & 0x20) == 0;

    const bool irq2 = ca2IsInput && irqA2Flag && ((registers.cra & 0x08) != 0);

    irqA = irq1 || irq2;
}

void MC6821::updateIRQB()
{
    const bool irq1 = irqB1Flag && ((registers.crb & 0x01) != 0);

    const bool cb2IsInput = (registers.crb & 0x20) == 0;

    const bool irq2 = cb2IsInput && irqB2Flag && ((registers.crb & 0x08) != 0);

    irqB = irq1 || irq2;
}

void MC6821::setResetLine(bool high)
{
    if (resetLine == high)
        return;

    resetLine = high;

    if (!resetLine)
        reset();
}
