// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Drive/D1571CIA.h"

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

D1571CIA::~D1571CIA() = default;

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
    registers.interruptEnable = 0x00;
    registers.controlRegisterA = 0x00;
    registers.controlRegisterB = 0x00;

    // IRQ reset
    interruptStatus = 0x00;

    // Reset alarms
    todAlarm10th = 0x00;
    todAlarmSeconds = 0x00;
    todAlarmMinutes = 0x00;
    todAlarmHours = 0x00;

    // Reset timers
    timerACounter = 0;
    timerALatch = 0;
    timerARunning = false;
    timerBCounter = 0;
    timerBLatch = 0;
    timerBRunning = false;
}

void D1571CIA::tick(uint32_t cycles)
{
    while(cycles-- > 0)
    {
        // Timer A
        if (timerARunning && timerACounter > 0)
        {
            --timerACounter;

            registers.timerALowByte  = static_cast<uint8_t>(timerACounter & 0x00FF);
            registers.timerAHighByte = static_cast<uint8_t>((timerACounter >> 8) & 0x00FF);

            if (timerACounter == 0)
            {
                triggerInterrupt(INTERRUPT_TIMER_A);
                timerACounter = timerALatch;
            }
        }

        // Timer B
        if (timerBRunning && timerBCounter > 0)
        {
            --timerBCounter;

            registers.timerBLowByte  = static_cast<uint8_t>(timerBCounter & 0x00FF);
            registers.timerBHighByte = static_cast<uint8_t>((timerBCounter >> 8) & 0x00FF);

            if (timerBCounter == 0)
            {
                triggerInterrupt(INTERRUPT_TIMER_B);
                timerBCounter = timerBLatch;
            }
        }
    }
}

uint8_t D1571CIA::readRegister(uint16_t address)
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
        case 0x0C: return registers.serialData;
        case 0x0D:
        {
            uint8_t pending = interruptStatus & 0x1F;
            uint8_t result = pending;
            if (pending & registers.interruptEnable) result |= 0x80;

            // Clear the acknowledged sources (bits 0–4 that were set)
            interruptStatus &= static_cast<uint8_t>(~pending);

            // Update master bit
            refreshMasterBit();

            return result;
        }
        case 0x0E: return registers.controlRegisterA;
        case 0x0F: return registers.controlRegisterB;
        default: return 0xFF;
    }
    return 0xFF;
}

void D1571CIA::writeRegister(uint16_t address, uint8_t value)
{
    switch(address)
    {
        case 0x00: registers.portA = value; break;
        case 0x01: registers.portB = value; break;
        case 0x02: registers.ddrA = value; break;
        case 0x03: registers.ddrB = value; break;
        case 0x04:
        {
            registers.timerALowByte = value;
            timerACounter = (timerACounter & 0xFF00) | value;
            break;
        }
        case 0x05:
        {
            registers.timerAHighByte = value;
            timerACounter = static_cast<uint16_t>((registers.timerAHighByte) << 8) | static_cast<uint16_t>(registers.timerALowByte);

            timerALatch = timerACounter;

            // Start Timer A
            timerARunning = true;
            break;
        }
        case 0x06:
        {
            registers.timerBLowByte = value;
            timerBCounter = (timerBCounter & 0xFF00) | value;
            break;
        }
        case 0x07:
        {
            registers.timerBHighByte = value;
            timerBCounter = static_cast<uint16_t>((registers.timerBHighByte) << 8) | static_cast<uint16_t>(registers.timerBLowByte);

            timerBLatch = timerBCounter;

            // Start Timer B
            timerBRunning = true;
            break;
        }
        case 0x08: registers.tod10th = value; break;
        case 0x09: registers.todSeconds = value; break;
        case 0x0A: registers.todMinutes = value; break;
        case 0x0B: registers.todHours = value; break;
        case 0x0C: registers.serialData = value; break;
        case 0x0D:
        {
            uint8_t mask = value & 0x1F;
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
        case 0x0E: registers.controlRegisterA = value; break;
        case 0x0F: registers.controlRegisterB = value; break;
        default: break;
    }
}

void D1571CIA::triggerInterrupt(InterruptBit bit)
{
    interruptStatus |= bit;
    refreshMasterBit();
}

void D1571CIA::refreshMasterBit()
{
    // Refresh master bit 7
    if ((interruptStatus & 0x1F) != 0) interruptStatus |= 0x80;
    else interruptStatus &= ~0x80;
}
