// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "D1571CIA.h"

D1571CIA::D1571CIA() :
    parentPeripheral(nullptr),
    timerACounter(0),
    timerALatch(0),
    timerBCounter(0),
    timerBLatch(0),
    timerARunning(false),
    timerBRunning(false)
{

}

D1571CIA::~D1571CIA()
{

}

void D1571CIA::reset()
{
    // Initialize all registers
    registers.portA = 0x00;
    registers.portB = 0x00;
    registers.ddrA = 0x00;
    registers.ddrB = 0x00;
    registers.timerALowByte = 0x00;
    registers.timerAHighByte = 0x00;
    registers.timerBLowByte = 0x00;
    registers.timerBHighByte = 0x00;
    registers.tod10th = 0x00;
    registers.todSeconds = 0x00;
    registers.todMinutes = 0x00;
    registers.todHours = 0x00;
    registers.serialData = 0x00;
    registers.interruptControl = 0x00;
    registers.controlRegisterA = 0x00;
    registers.controlRegisterA = 0x00;
}

void D1571CIA::tick()
{

}

uint8_t D1571CIA::readRegister(uint16_t address)
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
        case 0x0F:
        default: break;
    }
    return 0xFF;
}

void D1571CIA::writeRegister(uint16_t address, uint8_t value)
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
        case 0x0F:
        default: break;
    }
}
