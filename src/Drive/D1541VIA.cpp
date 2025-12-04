// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Drive/D1541VIA.h"
#include "Peripheral.h"
#include "IECBUS.h"

D1541VIA::D1541VIA() :
    viaRole(VIARole::Unknown),
    srCount(0)
{
    reset();
}

D1541VIA::~D1541VIA() = default;

void D1541VIA::attachPeripheralInstance(Peripheral* parentPeripheral, VIARole viaRole)
{
    this->parentPeripheral = parentPeripheral;
    this->viaRole = viaRole;
}

void D1541VIA::reset()
{
    // Reset all registers
    registers.portA                     = 0x00;
    registers.portB                     = 0x00;
    registers.ddrA                      = 0x00;
    registers.ddrB                      = 0x00;
    registers.timer1CounterLow          = 0x00;
    registers.timer1CounterHigh         = 0x00;
    registers.timer1LatchLow            = 0x00;
    registers.timer1LatchHigh           = 0x00;
    registers.timer2CounterLow          = 0x00;
    registers.timer2CounterHigh         = 0x00;
    registers.shiftRegister             = 0x00;
    registers.auxillaryControlRegister  = 0x00;
    registers.peripheralControlRegister = 0x00;
    registers.interruptFlag             = 0x00;
    registers.interruptEnable           = 0x00;

    // Shift registers
    srCount = 0;
}

void D1541VIA::tick(uint32_t cycles)
{
    while(cycles-- > 0)
    {
        // Timer 1
        if (t1Running)
        {
            if (t1Counter > 0)
            {
                --t1Counter;

                // Reflect back into the visible counter registers
                registers.timer1CounterLow  = static_cast<uint8_t>(t1Counter & 0x00FF);
                registers.timer1CounterHigh = static_cast<uint8_t>((t1Counter >> 8) & 0x00FF);

                if (t1Counter == 0)
                {
                    // Set IFR6
                     triggerInterrupt(IFR_TIMER1);

                    // Check ACR bit 6 to decide one-shot vs continuous
                    bool t1Continuous = (registers.auxillaryControlRegister & 0x40) != 0;

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

                registers.timer2CounterLow  = static_cast<uint8_t>(t2Counter & 0x00FF);
                registers.timer2CounterHigh = static_cast<uint8_t>((t2Counter >> 8) & 0x00FF);

                if (t2Counter == 0)
                {
                    // Set IFR bit 5
                     triggerInterrupt(IFR_TIMER2);

                    // Free-running: reload from latch and keep going
                    t2Counter = t2Latch;
                }
            }
        }
    }
}

uint8_t D1541VIA::readRegister(uint16_t address)
{
    switch(address)
    {
        case 0x00:
        {
            if (viaRole == VIARole::VIA1_DataHandler && parentPeripheral && parentPeripheral->bus)
            {
                bool clk  = parentPeripheral->bus->readClkLine();
                bool data = parentPeripheral->bus->readDataLine();
                registers.portB = (registers.portB & 0x3F) | ((clk ? 1 : 0) << 6) | ((data ? 1 : 0) << 7);
            }
            return registers.portB;
        }
        case 0x01:
        {
            if (viaRole == VIARole::VIA2_AtnMonitor && parentPeripheral && parentPeripheral->bus)
            {
                bool atn = parentPeripheral->bus->readAtnLine();
                registers.portA = (registers.portA & ~(1 << 3)) | ((atn ? 1 : 0) << 3); // bit 3 = ATN
                bool srq = parentPeripheral->bus->readSrqLine();
                registers.portA = (registers.portA & ~(1 << 2)) | ((srq ? 1 : 0) << 2);
            }
            return registers.portA;
        }
        case 0x02: return registers.ddrB;
        case 0x03: return registers.ddrA;
        case 0x04: return registers.timer1CounterLow;
        case 0x05: return registers.timer1CounterHigh;
        case 0x06: return registers.timer1LatchLow;
        case 0x07: return registers.timer1LatchHigh;
        case 0x08: return registers.timer2CounterLow;
        case 0x09: return registers.timer2CounterHigh;
        case 0x0A: return registers.shiftRegister;
        case 0x0B: return registers.auxillaryControlRegister;
        case 0x0C: return registers.peripheralControlRegister;
        case 0x0D: return registers.interruptFlag;
        case 0x0E: return registers.interruptEnable;
        case 0x0F: return registers.portA;
        default: return 0xFF;
    }
}

void D1541VIA::writeRegister(uint16_t address, uint8_t value)
{
    switch(address)
    {
        case 0x00:
        {
            registers.portB = (registers.portB & ~registers.ddrB) | (value & registers.ddrB);
            if (viaRole == VIARole::VIA1_DataHandler && parentPeripheral)
            {
                // Bit 6 = Clk, Bit 7 = Data
                parentPeripheral->peripheralAssertClk((value & (1 << 6)) == 0);
                parentPeripheral->peripheralAssertData((value & (1 << 7)) == 0);
            }
            break;
        }
        case 0x01:
        {
            registers.portA = (registers.portA & ~registers.ddrA) | (value & registers.ddrA);

            // if this VIA is the one that drives SRQ/ATN, push the lines out
            if (viaRole == VIARole::VIA2_AtnMonitor && parentPeripheral)
            {
                bool atnOut = (registers.portA & (1 << 3)) != 0;
                bool srqOut = (registers.portA & (1 << 2)) != 0;

                parentPeripheral->peripheralAssertAtn(!atnOut);
                parentPeripheral->peripheralAssertSrq(!srqOut);
            }
            break;
        }
        case 0x02:
        {
            registers.ddrB = value;
            break;
        }
        case 0x03:
        {
            registers.ddrA = value;
            break;
        }
        case 0x04:
        {
            registers.timer1CounterLow = value;
            break;
        }
        case 0x05:
        {
            registers.timer1CounterHigh = value;
            break;
        }
        case 0x06:
        {
            registers.timer1LatchLow = value;
            break;
        }
        case 0x07:
        {
            registers.timer1LatchHigh = value;
            break;
        }
        case 0x08:
        {
            registers.timer2CounterLow = value;
            break;
        }
        case 0x09:
        {
            registers.timer2CounterHigh = value;
            break;
        }
        case 0x0A:
        {
            registers.shiftRegister = value;
            break;
        }
        case 0x0B:
        {
            registers.auxillaryControlRegister = value;
            break;
        }
        case 0x0C:
        {
            registers.peripheralControlRegister = value;
            break;
        }
        case 0x0D:
        {
            registers.interruptFlag &= ~value;
            break;
        }
        case 0x0E:
        {
            bool set = (value & 0x80) != 0;
            uint8_t mask = value & 0x7F;
            if (set)
            {
                registers.interruptEnable |= mask;
            }
            else
            {
                registers.interruptEnable &= ~mask;
            }
            break;
        }
        case 0x0F:
        {
            // Same as 0x01 but without driving ATN/SRQ
            registers.portA = (registers.portA & ~registers.ddrA) | (value & registers.ddrA);
            break;
        }
        default: break;
    }
}

void D1541VIA::triggerInterrupt(uint8_t mask)
{
    registers.interruptFlag |= mask;
    refreshMasterBit();
}

void D1541VIA::clearIFR(uint8_t mask)
{
    registers.interruptFlag &= static_cast<uint8_t>(~mask);
    refreshMasterBit();
}

void D1541VIA::refreshMasterBit()
{
    const uint8_t pendingEnabled = registers.interruptFlag & registers.interruptEnable & 0x7F;

    if (pendingEnabled)
        registers.interruptFlag |= IFR_IRQ;
    else
        registers.interruptFlag &= static_cast<uint8_t>(~IFR_IRQ);
}
