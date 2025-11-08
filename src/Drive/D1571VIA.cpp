// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Drive/D1571VIA.h"

D1571VIA::D1571VIA() :
    parentPeripheral(nullptr),
    viaRole(VIARole::Unknown),
    t1Counter(0),
    t1Latch(0),
    t1Running(false),
    t2Counter(0),
    t2Latch(0),
    t2Running(false)
{

}

D1571VIA::~D1571VIA() = default;

void D1571VIA::attachPeripheralInstance(Peripheral* parentPeripheral, VIARole viaRole)
{
    this->parentPeripheral = parentPeripheral;
    this->viaRole = viaRole;
}

bool D1571VIA::checkIRQActive() const
{
    bool active = registers.interruptEnable & registers.interruptFlag & 0x7F;
    return active != 0;
}

void D1571VIA::reset()
{
    // Initialize registers
    registers.orbIRB = 0x00;
    registers.oraIRA = 0x00;
    registers.ddrB = 0x00;
    registers.ddrA = 0x00;
    registers.timer1CounterLowByte = 0x00;
    registers.timer1CounterHighByte = 0x00;
    registers.timer1LowLatch = 0x00;
    registers.timer1HighLatch = 0x00;
    registers.timer2CounterLowByte = 0x00;
    registers.timer2CounterHighByte = 0x00;
    registers.serialShift = 0x00;
    registers.auxControlRegister = 0x00;
    registers.peripheralControlRegister = 0x00;
    registers.interruptFlag = 0x00;
    registers.interruptEnable = 0x00;
    registers.oraIRANoHandshake = 0x00;

    t1Counter = 0;
    t1Latch   = 0;
    t1Running = false;

    t2Counter = 0;
    t2Latch   = 0;
    t2Running = false;
}

void D1571VIA::tick()
{
    // Timer 1
    if (t1Running)
    {
        if (t1Counter > 0)
        {
            --t1Counter;

            // Reflect back into the visible counter registers
            registers.timer1CounterLowByte  = static_cast<uint8_t>(t1Counter & 0x00FF);
            registers.timer1CounterHighByte = static_cast<uint8_t>((t1Counter >> 8) & 0x00FF);

            if (t1Counter == 0)
            {
                // Set IFR6 (Timer 1 interrupt)
                registers.interruptFlag |= 0x40;

                // Check ACR bit 6 to decide one-shot vs continuous
                bool t1Continuous = (registers.auxControlRegister & 0x40) != 0;

                if (t1Continuous)
                {
                    // Free-run: reload from latch and keep going
                    t1Counter = t1Latch;
                }
                else
                {
                    // One-shot: stop the timer
                    t1Running = false;
                }
            }
        }
    }

    // Timer 2
    if (t2Running)
    {
        if (t2Counter > 0)
        {
            --t2Counter;

            registers.timer2CounterLowByte  = static_cast<uint8_t>(t2Counter & 0x00FF);
            registers.timer2CounterHighByte = static_cast<uint8_t>((t2Counter >> 8) & 0x00FF);

            if (t2Counter == 0)
            {
                // Set IFR bit 5
                registers.interruptFlag |= 0x20;

                // Free-running: reload from latch and keep going
                t2Counter = t2Latch;
            }
        }
    }
}

uint8_t D1571VIA::readRegister(uint16_t address)
{
    switch(address)
    {
        case 0x00: return registers.orbIRB; break;
        case 0x01: return registers.oraIRA; break;
        case 0x02: return registers.ddrB; break;
        case 0x03: return registers.ddrA; break;
        case 0x04: return registers.timer1CounterLowByte; break;
        case 0x05: return registers.timer1CounterHighByte; break;
        case 0x06: return registers.timer1LowLatch; break;
        case 0x07: return registers.timer1HighLatch; break;
        case 0x08: return registers.timer2CounterLowByte; break;
        case 0x09: return registers.timer2CounterHighByte; break;
        case 0x0A: return registers.serialShift; break;
        case 0x0B: return registers.auxControlRegister; break;
        case 0x0C: return registers.peripheralControlRegister; break;
        case 0x0D: return registers.interruptFlag; break;
        case 0x0E: return registers.interruptEnable; break;
        case 0x0F: return registers.oraIRANoHandshake; break;
        default: break;
    }
    return 0xFF;
}

void D1571VIA::writeRegister(uint16_t address, uint8_t value)
{
    switch(address)
    {
        case 0x00: registers.orbIRB = value; break;
        case 0x01: registers.oraIRA = value; break;
        case 0x02: registers.ddrB = value; break;
        case 0x03: registers.ddrA = value; break;
        case 0x04:
        {
            registers.timer1CounterLowByte = value;
            t1Counter = (t1Counter & 0xFF00) | value;
            break;
        }
        case 0x05:
        {
            registers.timer1CounterHighByte = value;
            t1Counter = static_cast<uint16_t>((registers.timer1CounterHighByte << 8)) | static_cast<uint16_t>(registers.timer1CounterLowByte);

            // Start timer 1
            t1Running = true;

            // Clear T1 interrupt flag (IFR bit 6) when (re)loading the counter
            registers.interruptFlag &= static_cast<uint8_t>(~0x40);
            break;
        }
        case 0x06:
        {
            registers.timer1LowLatch = value;

            // Update latch
            t1Latch = (t1Latch & 0xFF00) | value;
            break;
        }
        case 0x07:
        {
            registers.timer1HighLatch = value;

            // Update latch
            t1Latch = static_cast<uint16_t>((registers.timer1HighLatch << 8)) | static_cast<uint16_t>(registers.timer1LowLatch);
            break;
        }
        case 0x08:
        {
            registers.timer2CounterLowByte = value;
            t2Counter = (t2Counter & 0xFF00) | value;
            break;
        }
        case 0x09:
        {
            registers.timer2CounterHighByte = value;
            t2Counter = static_cast<uint16_t>((registers.timer2CounterHighByte << 8)) | static_cast<uint16_t>(registers.timer2CounterLowByte);

            // Free-running latch: keep a copy for reload
            t2Latch = t2Counter;

            // Start Timer 2 when high byte is written
            t2Running = true;

            // Clear T2 IFR bit (bit 5) on reload
            registers.interruptFlag &= static_cast<uint8_t>(~0x20);
            break;
        }
        case 0x0A: registers.serialShift = value; break;
        case 0x0B: registers.auxControlRegister = value; break;
        case 0x0C: registers.peripheralControlRegister = value; break;
        case 0x0D:
        {
            uint8_t mask = value & 0x7F;
            // Clear any bits where a 1 was written
            registers.interruptFlag &= static_cast<uint8_t>(~mask);
            break;
        }
        case 0x0E:
        {
            uint8_t mask = value & 0x7F;
            if (value & 0x80)
            {
                registers.interruptEnable |= mask;
            }
            else
            {
                registers.interruptEnable &= static_cast<uint8_t>(~mask);
            }
            break;
        }
        case 0x0F: registers.oraIRANoHandshake = value; break;
        default: break;
    }
}
