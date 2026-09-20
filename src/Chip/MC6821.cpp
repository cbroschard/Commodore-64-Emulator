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
    ca2Input(false),
    ca2Output(false),
    ca2PulseActive(false),
    ca2PulseCycles(0),
    cb1(false),
    cb2Input(false),
    cb2Output(false),
    cb2PulseActive(false),
    cb2PulseCycles(0),
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

void MC6821::save(StateWriter& wrtr) const
{
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
    wrtr.writeBool(ca2Input);
    wrtr.writeBool(ca2Output);
    wrtr.writeBool(ca2PulseActive);

    wrtr.writeU32(ca2PulseCycles);

    wrtr.writeBool(cb1);
    wrtr.writeBool(cb2Input);
    wrtr.writeBool(cb2Output);
    wrtr.writeBool(cb2PulseActive);
    wrtr.writeU32(cb2PulseCycles);

    wrtr.writeBool(irqA1Flag);
    wrtr.writeBool(irqA2Flag);
    wrtr.writeBool(irqB1Flag);
    wrtr.writeBool(irqB2Flag);

    wrtr.writeBool(resetLine);
}

bool MC6821::load(StateReader& rdr)
{
    uint32_t ver = 0;
    if (!rdr.readU32(ver))              {  return false; }
    if (ver != 1)                       {  return false; }

    if (!rdr.readU8(registers.ora))     {  return false; }
    if (!rdr.readU8(registers.orb))     {  return false; }
    if (!rdr.readU8(registers.ddra))    {  return false; }
    if (!rdr.readU8(registers.ddrb))    {  return false; }
    if (!rdr.readU8(registers.cra))     {  return false; }
    if (!rdr.readU8(registers.crb))     {  return false; }

    if (!rdr.readU8(externalPinsA))     {  return false; }
    if (!rdr.readU8(externalPinsB))     {  return false; }

    if (!rdr.readBool(ca1))             {  return false; }
    if (!rdr.readBool(ca2Input))        {  return false; }
    if (!rdr.readBool(ca2Output))       {  return false; }
    if (!rdr.readBool(ca2PulseActive))  {  return false; }

    if (!rdr.readU32(ca2PulseCycles))   {  return false; }

    if (!rdr.readBool(cb1))             {  return false; }
    if (!rdr.readBool(cb2Input))        {  return false; }
    if (!rdr.readBool(cb2Output))       {  return false; }
    if (!rdr.readBool(cb2PulseActive))  {  return false; }

    if (!rdr.readU32(cb2PulseCycles))   {  return false; }

    if (!rdr.readBool(irqA1Flag))       {  return false; }
    if (!rdr.readBool(irqA2Flag))       {  return false; }
    if (!rdr.readBool(irqB1Flag))       {  return false; }
    if (!rdr.readBool(irqB2Flag))       {  return false; }

    if (!rdr.readBool(resetLine))       {  return false; }

    // Normalize post load
    registers.cra &= 0x3F;
    registers.crb &= 0x3F;

    updateIRQA();
    updateIRQB();

    synchronizeOutputs();

    return true;
}

void MC6821::reset()
{
    registers       = Registers{};

    ca1             = false;
    ca2Input        = false;
    ca2Output       = false;
    ca2PulseActive  = false;

    ca2PulseCycles  = 0;

    cb1             = false;
    cb2Input        = false;
    cb2Output       = false;
    cb2PulseActive  = false;

    cb2PulseCycles  = 0;

    irqA1Flag       = false;
    irqA2Flag       = false;
    irqB1Flag       = false;
    irqB2Flag       = false;

    irqA            = false;
    irqB            = false;

    updateIRQA();
    updateIRQB();

    synchronizeOutputs();
}

void MC6821::tick(uint32_t elapsedCycles)
{
    if (ca2PulseActive)
    {
        if (elapsedCycles >= ca2PulseCycles)
        {
            ca2PulseCycles = 0;
            ca2PulseActive = false;
            setCA2Output(true);
        }
        else
            ca2PulseCycles -= elapsedCycles;
    }

    if (cb2PulseActive)
    {
        if (elapsedCycles >= cb2PulseCycles)
        {
            cb2PulseCycles = 0;
            cb2PulseActive = false;
            setCB2Output(true);
        }
        else
            cb2PulseCycles -= elapsedCycles;
    }
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
            {
                registers.ddra = value;
                updatePortAOutput();
            }

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
            {
                registers.ddrb = value;
                updatePortBOutput();
            }

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

void MC6821::setPortAInputs(uint8_t value)
{
    externalPinsA = value;
}

void MC6821::setPortBInputs(uint8_t value)
{
    externalPinsB = value;
}

void MC6821::setPortADirection(uint8_t value)
{
    registers.ddra = value;
    updatePortAOutput();
}

void MC6821::setPortBDirection(uint8_t value)
{
    registers.ddrb = value;
    updatePortBOutput();
}

uint8_t MC6821::readPortA()
{
    const uint8_t value = (registers.ora & registers.ddra) | (externalPinsA & static_cast<uint8_t>(~registers.ddra));

    irqA1Flag = false;
    irqA2Flag = false;

    if (getCA2Mode() == C2Mode::Handshake)
        setCA2Output(false);
    else if (getCA2Mode() == C2Mode::Pulse)
    {
        setCA2Output(false);
        ca2PulseActive = true;
        ca2PulseCycles = 1;
    }

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

    updatePortAOutput();
}

void MC6821::writePortB(uint8_t value)
{
    registers.orb = value;

    updatePortBOutput();

    if (getCB2Mode() == C2Mode::Handshake)
        setCB2Output(false);
    else if (getCB2Mode() == C2Mode::Pulse)
    {
        setCB2Output(false);
        cb2PulseActive = true;
        cb2PulseCycles = 1;
    }
}

void MC6821::writeCRA(uint8_t value)
{
    registers.cra = value & 0x3F;

    const auto mode = static_cast<C2Mode>((registers.cra >> 3) & 0x07);

    // A control-mode change terminates any previous pulse.
    ca2PulseActive = false;
    ca2PulseCycles = 0;

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
            setCA2Output(true);
            break;

        case C2Mode::Pulse:
            setCA2Output(true);
            ca2PulseActive = false;
            ca2PulseCycles = 0;
            break;

        case C2Mode::ForceLow:
            setCA2Output(false);
            break;

        case C2Mode::ForceHigh:
            setCA2Output(true);
            break;
    }

    updateIRQA();
}

void MC6821::writeCRB(uint8_t value)
{
    registers.crb = value & 0x3F;

    const auto mode = static_cast<C2Mode>((registers.crb >> 3) & 0x07);

    // A control-mode change terminates any previous pulse.
    cb2PulseActive = false;
    cb2PulseCycles = 0;

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
            setCB2Output(true);
            break;

        case C2Mode::Pulse:
            setCB2Output(true);
            cb2PulseActive = false;
            cb2PulseCycles = 0;
            break;

        case C2Mode::ForceLow:
            setCB2Output(false);
            break;

        case C2Mode::ForceHigh:
            setCB2Output(true);
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

        if (getCA2Mode() == C2Mode::Handshake)
            setCA2Output(true);

        updateIRQA();
    }
}

void MC6821::setCA2Input(bool level)
{
    if (registers.cra & 0x20)
        return;

    const bool rising  = !ca2Input && level;
    const bool falling = ca2Input && !level;

    ca2Input = level;

    const bool risingSelected = (registers.cra & 0x10) != 0;

    if ((risingSelected && rising) || (!risingSelected && falling))
    {
        irqA2Flag = true;
        updateIRQA();
    }
}

void MC6821::setCA2Output(bool level)
{
    if (ca2Output == level)
        return;

    ca2Output = level;

    if (ca2OutputCallback)
        ca2OutputCallback(level);
}

void MC6821::setCB1(bool level)
{
    const bool rising  = !cb1 && level;
    const bool falling = cb1 && !level;

    cb1 = level;

    const bool risingSelected = (registers.crb & 0x02) != 0;

    if ((risingSelected && rising) ||
        (!risingSelected && falling))
    {
        irqB1Flag = true;

        if (getCB2Mode() == C2Mode::Handshake)
            setCB2Output(true);

        updateIRQB();
    }
}

void MC6821::setCB2Input(bool level)
{
    if (registers.crb & 0x20)
        return;

    const bool rising  = !cb2Input && level;
    const bool falling = cb2Input && !level;

    cb2Input = level;

    const bool risingSelected = (registers.crb & 0x10) != 0;

    if ((risingSelected && rising) || (!risingSelected && falling))
    {
        irqB2Flag = true;
        updateIRQB();
    }
}

void MC6821::setCB2Output(bool level)
{
    if (cb2Output == level)
        return;

    cb2Output = level;

    if (cb2OutputCallback)
        cb2OutputCallback(level);
}

void MC6821::updatePortAOutput()
{
    if (portAOutputCallback)
        portAOutputCallback(registers.ora, registers.ddra);
}

void MC6821::updatePortBOutput()
{
    if (portBOutputCallback)
        portBOutputCallback(registers.orb, registers.ddrb);
}

void MC6821::setIRQAOutput(bool level)
{
    if (irqA == level)
        return;

    irqA = level;

    if (irqACallback)
        irqACallback(level);
}

void MC6821::setIRQBOutput(bool level)
{
    if (irqB == level)
        return;

    irqB = level;

    if (irqBCallback)
        irqBCallback(level);
}

void MC6821::updateIRQA()
{
    const bool irq1 = irqA1Flag && ((registers.cra & 0x01) != 0);

    const bool ca2IsInput = (registers.cra & 0x20) == 0;

    const bool irq2 = ca2IsInput && irqA2Flag && ((registers.cra & 0x08) != 0);

    setIRQAOutput(irq1 || irq2);
}

void MC6821::updateIRQB()
{
    const bool irq1 = irqB1Flag && ((registers.crb & 0x01) != 0);

    const bool cb2IsInput = (registers.crb & 0x20) == 0;

    const bool irq2 = cb2IsInput && irqB2Flag && ((registers.crb & 0x08) != 0);

    setIRQBOutput(irq1 || irq2);
}

void MC6821::setResetLine(bool high)
{
    if (resetLine == high)
        return;

    resetLine = high;

    if (!resetLine)
        reset();
}

void MC6821::setCA2OutputCallback(std::function<void(bool)> callback)
{
    ca2OutputCallback = std::move(callback);

    if (ca2OutputCallback)
        ca2OutputCallback(ca2Output);
}

void MC6821::setCB2OutputCallback(std::function<void(bool)> callback)
{
    cb2OutputCallback = std::move(callback);

    if (cb2OutputCallback)
        cb2OutputCallback(cb2Output);
}

void MC6821::setIRQACallback(std::function<void(bool)> callback)
{
    irqACallback = std::move(callback);

    if (irqACallback)
        irqACallback(irqA);
}

void MC6821::setIRQBCallback(std::function<void(bool)> callback)
{
    irqBCallback = std::move(callback);

    if (irqBCallback)
        irqBCallback(irqB);
}

void MC6821::setPortAOutputCallback(PortOutputCallback callback)
{
    portAOutputCallback = std::move(callback);

    if (portAOutputCallback)
        portAOutputCallback(registers.ora, registers.ddra);
}

void MC6821::setPortBOutputCallback(PortOutputCallback callback)
{
    portBOutputCallback = std::move(callback);

    if (portBOutputCallback)
        portBOutputCallback(registers.orb, registers.ddrb);
}

void MC6821::synchronizeOutputs()
{
    updatePortAOutput();
    updatePortBOutput();

    if (ca2OutputCallback)
        ca2OutputCallback(ca2Output);

    if (cb2OutputCallback)
        cb2OutputCallback(cb2Output);

    if (irqACallback)
        irqACallback(irqA);

    if (irqBCallback)
        irqBCallback(irqB);
}
