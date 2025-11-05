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
    portA = 0;
    portB = 0;
    ddrA = 0;
    ddrB = 0;
    timer1CounterLow = 0;
    timer1CounterHigh = 0;
    timer1LatchLow = 0;
    timer1LatchHigh = 0;
    timer2CounterLowByte = 0;
    timer2CounterHighByte = 0;
    shiftRegister = 0;
    auxiliaryControlRegister = 0;
    peripheralControlRegister = 0;
    interruptFlagRegister = 0;
    interruptEnableRegister = 0;
    srCount = 0;
}

void D1541VIA::tick()
{
    // ----- Timer 1 -----
    {
        uint16_t t1 = (static_cast<uint16_t>(timer1CounterHigh) << 8)
                    |  static_cast<uint16_t>(timer1CounterLow);

        if (t1 > 0)
        {
            --t1;
            timer1CounterLow  =  t1 & 0xFF;
            timer1CounterHigh = (t1 >> 8) & 0xFF;

            if (t1 == 0)
            {
                // Set T1 interrupt flag
                interruptFlagRegister |= IFR_T1;

                // Continuous mode? (ACR bit 4)
                bool continuous = (auxiliaryControlRegister & (1 << 4)) != 0;
                if (continuous)
                {
                    uint16_t lat = (static_cast<uint16_t>(timer1LatchHigh) << 8)
                                 |  static_cast<uint16_t>(timer1LatchLow);
                    timer1CounterLow  =  lat & 0xFF;
                    timer1CounterHigh = (lat >> 8) & 0xFF;
                }
            }
        }
    }

    // ----- Timer 2 -----
    {
        // Treat T2 as a 16-bit down-counter just like T1
        uint16_t t2 = (static_cast<uint16_t>(timer2CounterHighByte) << 8)
                    |  static_cast<uint16_t>(timer2CounterLowByte);

        if (t2 > 0)
        {
            --t2;
            timer2CounterLowByte  =  t2 & 0xFF;
            timer2CounterHighByte = (t2 >> 8) & 0xFF;

            if (t2 == 0)
            {
                // Set T2 interrupt flag
                interruptFlagRegister |= IFR_T2;

                // For now: leave it at 0. If you later model pulse-counter mode
                // or reload behavior, you can extend this.
            }
        }
    }

    // ----- Shift register -----
    bool srEnabled = (auxiliaryControlRegister & (1 << 2)) != 0;
    if (srEnabled)
    {
        ++srCount;
        if (srCount >= 8)
        {
            interruptFlagRegister |= IFR_SR;
            srCount = 0;
        }
    }

    // ----- IRQ summary bit (bit 7 of IFR) -----
    bool anyEnabledPending = (interruptFlagRegister & interruptEnableRegister & 0x7F) != 0;
    if (anyEnabledPending)
        interruptFlagRegister |= IFR_IRQ;   // bit 7
    else
        interruptFlagRegister &= ~IFR_IRQ;
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
                portB = (portB & 0x3F) | ((clk ? 1 : 0) << 6) | ((data ? 1 : 0) << 7);
            }
            return portB;
        }
        case 0x01:
        {
            if (viaRole == VIARole::VIA2_AtnMonitor && parentPeripheral && parentPeripheral->bus)
            {
                bool atn = parentPeripheral->bus->readAtnLine();
                portA = (portA & ~(1 << 3)) | ((atn ? 1 : 0) << 3); // bit 3 = ATN
                bool srq = parentPeripheral->bus->readSrqLine();
                portA = (portA & ~(1 << 2)) | ((srq ? 1 : 0) << 2);
            }
            return portA;
        }
        case 0x02:
        {
            return ddrB;
        }
        case 0x03:
        {
            return ddrA;
        }
        case 0x04:
        {
            return timer1CounterLow;
        }
        case 0x05:
        {
            return timer1CounterHigh;
        }
        case 0x06:
        {
            return timer1LatchLow;
        }
        case 0x07:
        {
            return timer1LatchHigh;
        }
        case 0x08:
        {
            return timer2CounterLowByte;
        }
        case 0x09:
        {
            return timer2CounterHighByte;
        }
        case 0x0A:
        {
            return shiftRegister;
        }
        case 0x0B:
        {
            return auxiliaryControlRegister;
        }
        case 0x0C:
        {
            return peripheralControlRegister;
        }
        case 0x0D:
        {
            return interruptFlagRegister;
        }
        case 0x0E:
        {
            return interruptEnableRegister;
        }
        case 0x0F:
        {
            return portA;
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
            portB = (portB & ~ddrB) | (value & ddrB);
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
            portA = (portA & ~ddrA) | (value & ddrA);

            // if this VIA is the one that drives SRQ/ATN, push the lines out
            if (viaRole == VIARole::VIA2_AtnMonitor && parentPeripheral)
            {
                bool atnOut = (portA & (1 << 3)) != 0;
                bool srqOut = (portA & (1 << 2)) != 0;

                parentPeripheral->peripheralAssertAtn(!atnOut);
                parentPeripheral->peripheralAssertSrq(!srqOut);
            }
            break;
        }
        case 0x02:
        {
            ddrB = value;
            break;
        }
        case 0x03:
        {
            ddrA = value;
            break;
        }
        case 0x04:
        {
            timer1CounterLow = value;
            break;
        }
        case 0x05:
        {
            timer1CounterHigh = value;
            break;
        }
        case 0x06:
        {
            timer1LatchLow = value;
            break;
        }
        case 0x07:
        {
            timer1LatchHigh = value;
            break;
        }
        case 0x08:
        {
            timer2CounterLowByte = value;
            break;
        }
        case 0x09:
        {
            timer2CounterHighByte = value;
            break;
        }
        case 0x0A:
        {
            shiftRegister = value;
            break;
        }
        case 0x0B:
        {
            auxiliaryControlRegister = value;
            break;
        }
        case 0x0C:
        {
            peripheralControlRegister = value;
            break;
        }
        case 0x0D:
        {
            interruptFlagRegister &= ~value;
            break;
        }
        case 0x0E:
        {
            bool set = (value & 0x80) != 0;
            uint8_t mask = value & 0x7F;
            if (set)
            {
                interruptEnableRegister |= mask;
            }
            else
            {
                interruptEnableRegister &= ~mask;
            }
            break;
        }
        case 0x0F:
        {
            // Same as 0x01 but without driving ATN/SRQ
            portA = (portA & ~ddrA) | (value & ddrA);
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
