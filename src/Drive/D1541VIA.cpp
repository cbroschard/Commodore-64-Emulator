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

void D1541VIA::attachLoggingInstance(Logging* logger)
{
    this->logger = logger;
}

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

void D1541VIA::tick()
{
    // ----- Timer 1 -----
    {
        uint16_t t1 = (static_cast<uint16_t>(registers.timer1CounterHigh) << 8)
                    |  static_cast<uint16_t>(registers.timer1CounterLow);

        if (t1 > 0)
        {
            --t1;
            registers.timer1CounterLow  =  t1 & 0xFF;
            registers.timer1CounterHigh = (t1 >> 8) & 0xFF;

            if (t1 == 0)
            {
                // Set T1 interrupt flag
                registers.interruptFlag |= IFR_T1;

                // Continuous mode? (ACR bit 4)
                bool continuous = (registers.auxillaryControlRegister & (1 << 4)) != 0;
                if (continuous)
                {
                    uint16_t lat = (static_cast<uint16_t>(registers.timer1LatchHigh) << 8)
                                 |  static_cast<uint16_t>(registers.timer1LatchLow);
                    registers.timer1CounterLow  =  lat & 0xFF;
                    registers.timer1CounterHigh = (lat >> 8) & 0xFF;
                }
            }
        }
    }

    // ----- Timer 2 -----
    {
        // Treat T2 as a 16-bit down-counter just like T1
        uint16_t t2 = (static_cast<uint16_t>(registers.timer2CounterHigh) << 8)
                    |  static_cast<uint16_t>(registers.timer2CounterLow);

        if (t2 > 0)
        {
            --t2;
            registers.timer2CounterLow  =  t2 & 0xFF;
            registers.timer2CounterHigh = (t2 >> 8) & 0xFF;

            if (t2 == 0)
            {
                // Set T2 interrupt flag
                registers.interruptFlag |= IFR_T2;
            }
        }
    }

    // ----- Shift register -----
    bool srEnabled = (registers.auxillaryControlRegister & (1 << 2)) != 0;
    if (srEnabled)
    {
        ++srCount;
        if (srCount >= 8)
        {
            registers.interruptFlag |= IFR_SR;
            srCount = 0;
        }
    }

    // ----- IRQ summary bit (bit 7 of IFR) -----
    bool anyEnabledPending = (registers.interruptFlag & registers.interruptEnable & 0x7F) != 0;
    if (anyEnabledPending)
        registers.interruptFlag |= IFR_IRQ;   // bit 7
    else
        registers.interruptFlag &= ~IFR_IRQ;
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
        case 0x02:
        {
            return registers.ddrB;
        }
        case 0x03:
        {
            return registers.ddrA;
        }
        case 0x04:
        {
            return registers.timer1CounterLow;
        }
        case 0x05:
        {
            return registers.timer1CounterHigh;
        }
        case 0x06:
        {
            return registers.timer1LatchLow;
        }
        case 0x07:
        {
            return registers.timer1LatchHigh;
        }
        case 0x08:
        {
            return registers.timer2CounterLow;
        }
        case 0x09:
        {
            return registers.timer2CounterHigh;
        }
        case 0x0A:
        {
            return registers.shiftRegister;
        }
        case 0x0B:
        {
            return registers.auxillaryControlRegister;
        }
        case 0x0C:
        {
            return registers.peripheralControlRegister;
        }
        case 0x0D:
        {
            return registers.interruptFlag;
        }
        case 0x0E:
        {
            return registers.interruptEnable;
        }
        case 0x0F:
        {
            return registers.portA;
        }
        default:
        {
            if (logger)
            {
                logger->WriteLog("Error: attempted to read to undefined VIA area, returning 0xFF!");
            }
            return 0xFF;
        }
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
        default:
        {
            if (logger)
            {
                logger->WriteLog("Error: Attempted to write to undefined VIA1 area!");
            }
            break;
        }
    }
}
