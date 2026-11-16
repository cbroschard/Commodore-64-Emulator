// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Chip/MOS6530.h"

MOS6530::MOS6530()
{
    ram.fill(0x00);
    reset();
}

MOS6530::~MOS6530() = default;

void MOS6530::reset()
{
    // IO registers
    registers.portA             = 0x00;
    registers.ddrA              = 0x00;
    registers.portB             = 0x00;
    registers.ddrB              = 0x00;

    // Timer
    registers.timer             = 0x00;

    // Interrupt
    registers.interruptFlags    = 0x00;
    registers.timerIRQEnabled   = false;

    // Timer runtime state
    timerPrescaler              = 1;
    timerDivider                = 0;
    timerRunning                = false;
    timerUnderflowed            = false;

    // External port inputs
    portAInput                  = 0xFF;
    portBInput                  = 0xFF;
}

void MOS6530::save(StateWriter& wrtr) const
{
    // Dump registers
    wrtr.writeU8(registers.portA);
    wrtr.writeU8(registers.ddrA);
    wrtr.writeU8(registers.portB);
    wrtr.writeU8(registers.ddrB);
    wrtr.writeU8(registers.timer);
    wrtr.writeU8(registers.interruptFlags);
    wrtr.writeBool(registers.timerIRQEnabled);

    // Dump timer runtime state
    wrtr.writeU16(timerPrescaler);
    wrtr.writeU16(timerDivider);
    wrtr.writeBool(timerRunning);
    wrtr.writeBool(timerUnderflowed);

    // Dump inputs
    wrtr.writeU8(portAInput);
    wrtr.writeU8(portBInput);

    // Dump RAM
    for (uint8_t value : ram)
        wrtr.writeU8(value);
}

bool MOS6530::load(StateReader& rdr)
{
    // Load all registers
    if (!rdr.readU8(registers.portA))
        return false;
    if (!rdr.readU8(registers.ddrA))
        return false;
    if (!rdr.readU8(registers.portB))
       return false;
    if (!rdr.readU8(registers.ddrB))
        return false;
    if (!rdr.readU8(registers.timer))
        return false;
    if (!rdr.readU8(registers.interruptFlags))
        return false;
    if (!rdr.readBool(registers.timerIRQEnabled))
        return false;

    // Load timer runtime state
    if (!rdr.readU16(timerPrescaler))
        return false;
    if (!rdr.readU16(timerDivider))
        return false;
    if (!rdr.readBool(timerRunning))
        return false;
    if (!rdr.readBool(timerUnderflowed))
        return false;

    // Load inputs
    if (!rdr.readU8(portAInput))
        return false;
    if (!rdr.readU8(portBInput))
        return false;

    // Load RAM
    for (uint8_t& value : ram)
    {
        if (!rdr.readU8(value))
            return false;
    }

    return true;
}

void MOS6530::tick(uint32_t cycles)
{
    if (!timerRunning || cycles == 0)
        return;

    while (cycles-- > 0)
    {
        if (timerDivider > 0)
            --timerDivider;

        if (timerDivider != 0)
            continue;

        if (registers.timer == 0 && !timerUnderflowed)
        {
            // Timer underflow
            registers.timer = 0xFF;
            registers.interruptFlags |= 0x80;

            timerUnderflowed = true;

            // After underflow, decrement every CPU cycle
            timerPrescaler = 1;
        }
        else
            --registers.timer;

        timerDivider = timerPrescaler;
    }
}

uint8_t MOS6530::read(uint8_t address)
{
    address &= 0x0F;

    // Port A/B registers
    if ((address & 0x04) == 0)
    {
        switch(address & 0x03)
        {
            case 0x00:
                return (registers.portA & registers.ddrA) | (portAInput & ~registers.ddrA);
            case 0x01:
                return registers.ddrA;
            case 0x02:
                return (registers.portB & registers.ddrB) | (portBInput & ~registers.ddrB);
            case 0x03:
                return registers.ddrB;
        }
    }

    // Timer and interrupt registers
    if ((address & 0x04) != 0)
    {
        // Even addresses: timer counter
        if ((address & 0x01) == 0)
        {
            const uint8_t value = registers.timer;

            // A3 controls timer IRQ enable
            registers.timerIRQEnabled = (address & 0x08) != 0;

            // Reading the timer clears its interrupt flag
            registers.interruptFlags &= ~0x80;

            return value;
        }

        return registers.interruptFlags & 0x80;
    }

    return 0xFF;
}

void MOS6530::write(uint8_t address, uint8_t value)
{
    address &= 0x0F;

    // Port A/B registers
    if ((address & 0x04) == 0)
    {
        switch (address & 0x03)
        {
            case 0x00: // Port A output latch
                registers.portA = value;
                return;

            case 0x01: // Port A data direction
                registers.ddrA = value;
                return;

            case 0x02: // Port B output latch
                registers.portB = value;
                return;

            case 0x03: // Port B data direction
                registers.ddrB = value;
                return;
        }
    }

    // Timer registers
    if ((address & 0x04) != 0)
    {
        static constexpr uint16_t prescalers[4] =
        {
            1, 8, 64, 1024
        };

        registers.timer = value;

        timerPrescaler = prescalers[address & 0x03];
        timerDivider = timerPrescaler;

        timerRunning = true;
        timerUnderflowed = false;

        // Address bit 3 controls timer IRQ enable
        registers.timerIRQEnabled = (address & 0x08) != 0;

        // Clear pending timer interrupt
        registers.interruptFlags &= 0x7F;

        return;
    }
}

uint8_t MOS6530::peek(uint8_t address) const
{
    address &= 0x0F;

    // Port A/B registers
    if ((address & 0x04) == 0)
    {
        switch(address & 0x03)
        {
            case 0x00:
                return (registers.portA & registers.ddrA) | (portAInput & ~registers.ddrA);
            case 0x01:
                return registers.ddrA;
            case 0x02:
                return (registers.portB & registers.ddrB) | (portBInput & ~registers.ddrB);
            case 0x03:
                return registers.ddrB;
        }
    }

    // Timer and interrupt registers
    if ((address & 0x04) != 0)
    {
        if ((address & 0x01) == 0)
            return registers.timer;

        return registers.interruptFlags & 0x80;
    }

    return 0xFF;
}


uint8_t MOS6530::readRAM(uint8_t address) const
{
    return ram[address & 0x3F];
}

void MOS6530::writeRAM(uint8_t address, uint8_t value)
{
    ram[address & 0x3F] = value;
}
