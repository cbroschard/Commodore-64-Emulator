// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Chip/MOS6532.h"

MOS6532::MOS6532()
{
    ram.fill(0x00);
    reset();
}

MOS6532::~MOS6532() = default;

void MOS6532::reset()
{
    // I/O registers
    registers.portA = 0x00;
    registers.ddrA = 0x00;
    registers.portB = 0x00;
    registers.ddrB = 0x00;

    // Timer
    registers.timer = 0x00;
    timerPrescaler = 1;
    timerDivider = 0;
    timerRunning = false;
    timerUnderflowed = false;

    // Interrupts
    registers.interruptFlags = 0x00;
    registers.timerIRQEnabled = false;
    registers.pa7IRQEnabled = false;
    registers.pa7PositiveEdge = false;

    // External input defaults
    portAInput = 0xFF;
    portBInput = 0xFF;

    // Initialize edge detection from current input
    previousPA7 = (portAInput & 0x80) != 0;
}

void MOS6532::save(StateWriter& wrtr) const
{
    wrtr.writeU8(registers.portA);
    wrtr.writeU8(registers.ddrA);
    wrtr.writeU8(registers.portB);
    wrtr.writeU8(registers.ddrB);
    wrtr.writeU8(registers.timer);
    wrtr.writeU8(registers.interruptFlags);
    wrtr.writeBool(registers.timerIRQEnabled);
    wrtr.writeBool(registers.pa7IRQEnabled);
    wrtr.writeBool(registers.pa7PositiveEdge);

    wrtr.writeU16(timerPrescaler);
    wrtr.writeU16(timerDivider);
    wrtr.writeBool(timerRunning);
    wrtr.writeBool(timerUnderflowed);

    wrtr.writeBool(previousPA7);

    wrtr.writeU8(portAInput);
    wrtr.writeU8(portBInput);

    for (uint8_t value : ram)
        wrtr.writeU8(value);
}

bool MOS6532::load(StateReader& rdr)
{
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
    if (!rdr.readBool(registers.pa7IRQEnabled))
        return false;
    if (!rdr.readBool(registers.pa7PositiveEdge))
        return false;

    if (!rdr.readU16(timerPrescaler))
        return false;
    if (!rdr.readU16(timerDivider))
        return false;
    if (!rdr.readBool(timerRunning))
        return false;
    if (!rdr.readBool(timerUnderflowed))
        return false;

    if (!rdr.readBool(previousPA7))
        return false;

    if (!rdr.readU8(portAInput))
        return false;
    if (!rdr.readU8(portBInput))
        return false;

    for (uint8_t& value : ram)
    {
        if (!rdr.readU8(value))
            return false;
    }

    return true;
}


void MOS6532::tick(uint32_t cycles)
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

uint8_t MOS6532::read(uint8_t address)
{
    address &= 0x1F;

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

        // Odd addresses: interrupt flags
        const uint8_t flags = registers.interruptFlags & 0xC0;

        // Reading flags clears PA7 edge flag
        registers.interruptFlags &= ~0x40;

        return flags;
    }

    return 0xFF;
}


void MOS6532::write(uint8_t address, uint8_t value)
{
    address &= 0x1F;

    // Port A/B registers
    if ((address & 0x04) == 0)
    {
        switch (address & 0x03)
        {
            case 0x00: // Port A output latch
                registers.portA = value;
                updatePA7();
                return;

            case 0x01: // Port A data direction
                registers.ddrA = value;
                updatePA7();
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
    if ((address & 0x14) == 0x14)
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

    // PA7 edge detection control
    if ((address & 0x14) == 0x04)
    {
        // Address bit 1 enables PA7 IRQ
        registers.pa7IRQEnabled = (address & 0x02) != 0;

        // Address bit 0 selects edge polarity
        registers.pa7PositiveEdge = (address & 0x01) != 0;

        return;
    }
}

uint8_t MOS6532::peek(uint8_t address) const
{
    address &= 0x1F;

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

        return registers.interruptFlags & 0xC0;
    }

    return 0xFF;
}


uint8_t MOS6532::readRAM(uint8_t address) const
{
    return ram[address & 0x7F];
}

void MOS6532::writeRAM(uint8_t address, uint8_t value)
{
    ram[address & 0x7F] = value;
}

void MOS6532::setPortAInput(uint8_t value)
{
    portAInput = value;
    updatePA7();
}

bool MOS6532::isIRQActive() const
{
    const bool timerIRQ = (registers.interruptFlags & 0x80) != 0 && registers.timerIRQEnabled;
    const bool pa7IRQ = (registers.interruptFlags & 0x40) != 0 && registers.pa7IRQEnabled;

    return timerIRQ || pa7IRQ;
}

bool MOS6532::getPA7Level() const
{
    // PA7 configured as output
    if (registers.ddrA & 0x80)
        return (registers.portA & 0x80) != 0;

    // PA7 configured as input
    return (portAInput & 0x80) != 0;
}

void MOS6532::updatePA7()
{
    const bool currentPA7 = getPA7Level();

    // Detect rising or falling transition
    const bool risingEdge = !previousPA7 && currentPA7;
    const bool fallingEdge = previousPA7 && !currentPA7;

    if (registers.pa7PositiveEdge)
    {
        if (risingEdge)
            registers.interruptFlags |= 0x40;
    }
    else
    {
        if (fallingEdge)
            registers.interruptFlags |= 0x40;
    }

    previousPA7 = currentPA7;
}
