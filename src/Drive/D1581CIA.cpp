// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Drive/D1581CIA.h"

D1581CIA::D1581CIA() :
    parentPeripheral(nullptr)
{

}

D1581CIA::~D1581CIA() = default;

void D1581CIA::reset()
{
    // Initialize all registers
    registers.portA              = 0x00;
    registers.portB              = 0x00;
    registers.ddrA               = 0x00;
    registers.ddrB               = 0x00;
    registers.timerALowByte      = 0x00;
    registers.timerAHighByte     = 0x00;
    registers.timerBLowByte      = 0x00;
    registers.timerBHighByte     = 0x00;
    registers.tod10th            = 0x00;
    registers.todSeconds         = 0x00;
    registers.todMinutes         = 0x00;
    registers.todHours           = 0x00;
    registers.serialData         = 0x00;
    registers.interruptEnable    = 0x00;
    registers.controlRegisterA   = 0x00;
    registers.controlRegisterB   = 0x00;

    // Reset timers
    timerACounter = 0x00;
    timerALatch   = 0x00;
    timerBCounter = 0x00;
    timerBLatch   = 0x00;

    timerARunning = false;
    timerBRunning = false;

    // Reset TOD
    todAlarm10th    = 0x00;
    todAlarmSeconds = 0x00;
    todAlarmMinutes = 0x00;
    todAlarmHours   = 0x00;
}

void D1581CIA::tick()
{

}

uint8_t D1581CIA::readRegister(uint16_t address)
{
    switch(address)
    {
        case 0x00: return registers.portA;
        case 0x01: return registers.portB;
        case 0x02: return registers.ddrA;
        case 0x03: return registers.ddrB;
        case 0x04: return registers.timerALowByte;
        case 0x05: return registers.timerAHighByte;
        case 0x06: return registers.timerBLowByte;
        case 0x07: return registers.timerBHighByte;
        case 0x08: return registers.tod10th;
        case 0x09: return registers.todSeconds;
        case 0x0A: return registers.todMinutes;
        case 0x0B: return registers.todHours;
        case 0x0C: return registers.serialData;;
        case 0x0D: return registers.interruptEnable;
        case 0x0E: return registers.controlRegisterA;
        case 0x0F: return registers.controlRegisterB;
        default: return 0xFF;
    }
    return 0xFF;
}

void D1581CIA::writeRegister(uint16_t address, uint8_t value)
{
    switch(address)
    {
        case 0x00: registers.portA = value; break;
        case 0x01: registers.portB = value; break;
        case 0x02: registers.ddrA = value; break;
        case 0x03: registers.ddrB = value; break;
        case 0x04: registers.timerALowByte = value; break;
        case 0x05: registers.timerAHighByte = value; break;
        case 0x06: registers.timerBLowByte = value; break;
        case 0x07: registers.timerBHighByte = value; break;
        case 0x08: registers.tod10th = value; break;
        case 0x09: registers.todSeconds = value; break;
        case 0x0A: registers.todMinutes = value; break;
        case 0x0B: registers.todHours = value; break;
        case 0x0C: registers.serialData = value; break;
        case 0x0D: registers.interruptEnable = value; break;
        case 0x0E: registers.controlRegisterA = value; break;
        case 0x0F: registers.controlRegisterB = value; break;
        default: break;
    }
}
