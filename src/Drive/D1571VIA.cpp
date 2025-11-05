// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "D1571VIA.h"

D1571VIA::D1571VIA() :
    parentPeripheral(nullptr),
    viaRole(VIARole::Unknown),
    t1Counter(0),
    t1Latch(0),
    t1Running(false),
    t2Counter(0),
    t2Latch(0)
{

}

D1571VIA::~D1571VIA()
{

}

void D1571VIA::attachPeripheralInstance(Peripheral* parentPeripheral, VIARole viaRole)
{
    this->parentPeripheral = parentPeripheral;
    this->viaRole = viaRole;
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
}

void D1571VIA::tick()
{

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
        case 0x00:
        case 0x01:
        case 0x02:
        case 0x03:
        case 0x04:
        case 0x05:
        case 0x06:
        case 0x07:
        case 0x08:
        case 0x09:
        case 0x0A:
        case 0x0B:
        case 0x0C:
        case 0x0D:
        case 0x0E:
        case 0x0F: registers.oraIRANoHandshake = value; break;
        default: break;
    }
}
