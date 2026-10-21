// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Chip/TPI6525.h"

TPI6525::TPI6525()
{
    reset();
}

TPI6525::~TPI6525() = default;

void TPI6525::reset()
{
    registers = {};

    portAInput = 0xFF;
    portBInput = 0xFF;
    portCInput = 0xFF;

    caLine = true;
    cbLine = true;

    caPulseRemaining = 0;
    cbPulseRemaining = 0;

    irqLine = true;
    interruptInputs = 0x1F;
    interruptStack = 0;

    serviceStack.fill(0);
    serviceDepth = 0;
}

uint8_t TPI6525::readRegister(uint8_t reg)
{
    switch (reg & 0x07)
    {
        case 0x00:
        {
            const uint8_t caMode = (registers.cr >> 4) & 0x03;
            if (caMode == 0)
                caLine = false;
            else if (caMode == 1)
            {
                caLine = false;
                caPulseRemaining = 1;
            }
            return readPort(registers.portA, registers.ddrA, portAInput);
        }
        case 0x01:
            return readPort(registers.portB, registers.ddrB, portBInput);
        case 0x02:
        {
            if (isInterruptMode())
            {
                uint8_t value = (registers.pendingInterrupt & 0x1F) | 0xC0;
                if (!irqLine)
                    value |= 0x20;
                return value;
            }
            else
                return readPort(registers.portC, registers.ddrC, portCInput);
        }
        case 0x03:
            return registers.ddrA;
        case 0x04:
            return registers.ddrB;
        case 0x05:
            return isInterruptMode() ? registers.imr : registers.ddrC;
        case 0x06:
            return registers.cr;
        case 0x07:
            return acknowledgeInterrupt();
    }

    return 0xFF;
}

void TPI6525::writeRegister(uint8_t reg, uint8_t value)
{
    switch(reg & 0x07)
    {
        case 0x00:
        {
            registers.portA = value;
            break;
        }
        case 0x01:
        {
            const uint8_t cbMode = (registers.cr >> 6) & 0x03;
            if (cbMode == 0)
                cbLine = false;
            else if (cbMode == 1)
            {
                cbLine = false;
                cbPulseRemaining = 1;
            }

            registers.portB = value;
            break;
        }
        case 0x02:
            if (!isInterruptMode())
                registers.portC = value;
            else
                registers.pendingInterrupt &= static_cast<uint8_t>(value & 0x1F);
            break;
        case 0x03:
            registers.ddrA = value;
            break;
        case 0x04:
            registers.ddrB = value;
            break;
        case 0x05:
            if (isInterruptMode())
            {
                registers.imr = value & 0x1F;

                const uint8_t eligible = getEnabledInterrupts();

                for (int i = 4; i >= 0; --i)
                {
                    const uint8_t mask = static_cast<uint8_t>(1u << i);

                    if ((eligible & mask) != 0)
                        triggerInterrupt(mask);
                }
            }
            else
                registers.ddrC = value;
            break;
        case 0x06:
        {
            registers.cr = value;

            const uint8_t caMode = (registers.cr >> 4) & 0x03;
            const uint8_t cbMode = (registers.cr >> 6) & 0x03;

            if (caMode != 1)
                caPulseRemaining = 0;

            if (cbMode != 1)
                cbPulseRemaining = 0;

            if (caMode == 2)
                caLine = false;
            else if (caMode == 3)
                caLine = true;

            if (cbMode == 2)
                cbLine = false;
            else if (cbMode == 3)
                cbLine = true;

            break;
        }
        case 0x07:
            completeInterrupt();
            break;
    }
}

uint8_t TPI6525::peek(uint8_t reg) const
{
    switch(reg & 0x07)
    {
        case 0x00:
            return readPort(registers.portA, registers.ddrA, portAInput);
        case 0x01:
            return readPort(registers.portB, registers.ddrB, portBInput);
        case 0x02:
        {
            if (isInterruptMode())
            {
                uint8_t value = (registers.pendingInterrupt & 0x1F) | 0xC0;
                if (!irqLine)
                    value |= 0x20;
                return value;
            }
            else
                return readPort(registers.portC, registers.ddrC, portCInput);
        }
        case 0x03:
            return registers.ddrA;
        case 0x04:
            return registers.ddrB;
        case 0x05:
            return isInterruptMode() ? registers.imr : registers.ddrC;
        case 0x06:
            return registers.cr;
        case 0x07:
            return registers.air;
    }

    return 0xFF;
}

void TPI6525::tick(uint64_t cycles)
{
    while (cycles > 0)
    {
        if (caPulseRemaining > 0)
        {
            --caPulseRemaining;

            if (caPulseRemaining == 0)
                caLine = true;
        }

        if (cbPulseRemaining > 0)
        {
            --cbPulseRemaining;

            if (cbPulseRemaining == 0)
                cbLine = true;
        }

        --cycles;
    }
}

void TPI6525::setInterruptInput(uint8_t source, bool level)
{
    if (source >= 5)
        return;

    const uint8_t mask = static_cast<uint8_t>(1u << source);
    const bool previousLevel = (interruptInputs & mask) != 0;

    if (level)
        interruptInputs |= mask;
    else
        interruptInputs &= ~mask;

    const bool fallingEdge = previousLevel && !level;
    const bool risingEdge = !previousLevel && level;

    const bool risingSelectedI3 = (registers.cr & 0x04) != 0;
    const bool risingSelectedI4 = (registers.cr & 0x08) != 0;

    if (fallingEdge && (source <= 2))
        latchInterrupt(source);
    else if (source == 3)
    {
        const bool activeEdge = (risingSelectedI3 && risingEdge) || (!risingSelectedI3 && fallingEdge);
        if (activeEdge)
        {
            latchInterrupt(source);
            const uint8_t caMode = (registers.cr >> 4) & 0x03;
            if (caMode == 0)
                caLine = true;
        }
    }
    else if (source == 4)
    {
        const bool activeEdge = (risingSelectedI4 && risingEdge) || (!risingSelectedI4 && fallingEdge);
        if (activeEdge)
        {
            latchInterrupt(source);
            const uint8_t cbMode = (registers.cr >> 6) & 0x03;
            if (cbMode == 0)
                cbLine = true;
        }
    }
}

void TPI6525::latchInterrupt(uint8_t source)
{
    if (source >= 5)
        return;

    const uint8_t mask = static_cast<uint8_t>(1u << source);
    const bool alreadyPending = (registers.pendingInterrupt & mask) != 0;

    registers.pendingInterrupt |= mask;

    if (!alreadyPending)
        triggerInterrupt(mask);
}

void TPI6525::clearInterrupt(uint8_t source)
{
    if (source >= 5)
        return;

    const uint8_t mask = static_cast<uint8_t>(1u << source);
    registers.pendingInterrupt &= ~mask;
}

void TPI6525::setPortAInput(uint8_t value)
{
    portAInput = value;
}

void TPI6525::setPortBInput(uint8_t value)
{
    portBInput = value;
}

void TPI6525::setPortCInput(uint8_t value)
{
    portCInput = value;
}

void TPI6525::triggerInterrupt(uint8_t mask)
{
    if (mask == 0)
        return;

    if ((registers.imr & mask) == 0)
        return;

    interruptStack |= mask;

    if (isPriorityMode() && serviceDepth > 0)
    {
        const uint8_t currentService = serviceStack[serviceDepth - 1];
        const int servicePriority = getHighestPriorityInterrupt(currentService);
        const int newPriority = getHighestPriorityInterrupt(mask);

        if (newPriority <= servicePriority)
            return;
    }

    if (registers.air != 0 && !isPriorityMode())
        return;

    const uint8_t otherPending = getEnabledInterrupts() & static_cast<uint8_t>(~mask);
    const int otherPriority = getHighestPriorityInterrupt(otherPending);
    const int newPriority = getHighestPriorityInterrupt(mask);

    if (newPriority > otherPriority)
    {
        registers.air = mask;
        irqLine = false;
    }
}

uint8_t TPI6525::acknowledgeInterrupt()
{
    const uint8_t active = registers.air;

    if (isPriorityMode() && active != 0)
    {
        if (serviceDepth < serviceStack.size())
        {
            serviceStack[serviceDepth] = active;
            serviceDepth++;
        }
    }

    registers.air = 0;

    interruptStack &= ~active;
    registers.pendingInterrupt &= static_cast<uint8_t>(~active);

    if (!isPriorityMode())
    {
        registers.air = interruptStack;
        interruptStack = 0;
    }

    irqLine = (registers.air == 0);

    return active;
}

void TPI6525::completeInterrupt()
{
    if (isPriorityMode() && serviceDepth > 0)
    {
        --serviceDepth;
        serviceStack[serviceDepth] = 0;
    }

    int servicePriority = -1;

    if (serviceDepth > 0)
        servicePriority = getHighestPriorityInterrupt(serviceStack[serviceDepth - 1]);

    registers.air = 0;

    if (isPriorityMode() && interruptStack != 0)
    {
        for (int i = 4; i >= 0; --i)
        {
            const uint8_t mask = static_cast<uint8_t>(1u << i);

            if ((interruptStack & mask) != 0 && (getEnabledInterrupts() & mask) != 0 && i > servicePriority)
            {
                registers.air = mask;
                break;
            }
        }
    }

    irqLine = (registers.air == 0);
}

uint8_t TPI6525::readPort(uint8_t output, uint8_t direction, uint8_t input) const
{
    return static_cast<uint8_t>((output & direction) | (input & static_cast<uint8_t>(~direction)));
}

int TPI6525::getHighestPriorityInterrupt(uint8_t sources) const
{
    for (int i = 4; i >= 0; --i)
    {
        if ((sources & (1u << i)) != 0)
            return i;
    }

    return -1;
}
