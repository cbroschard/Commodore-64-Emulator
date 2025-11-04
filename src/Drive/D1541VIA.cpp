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
    timer2Counter = 0;
    shiftRegister = 0;
    auxiliaryControlRegister = 0;
    peripheralControlRegister = 0;
    interruptFlagRegister = 0;
    interruptEnableRegister = 0;
    reserved1 = 0;
    reserved2 = 0;
}

void D1541VIA::tick()
{
    {
        uint16_t t1 = (static_cast<uint16_t>(timer1CounterHigh) << 8) | timer1CounterLow;
        if (t1 > 0)
        {
            --t1;
            timer1CounterLow  =  t1 & 0xFF;
            timer1CounterHigh = (t1 >> 8) & 0xFF;
            if (t1 == 0)
            {
                interruptFlagRegister |= IFR_T1;
                bool cont = (auxiliaryControlRegister & (1 << 4)) != 0;
                uint16_t lat = (static_cast<uint16_t>(timer1LatchHigh) << 8) | timer1LatchLow;
                if (cont)
                {
                    timer1CounterLow  =  lat & 0xFF;
                    timer1CounterHigh = (lat >> 8) & 0xFF;
                }
            }
        }
    }

    {
        if (timer2Counter > 0)
        {
            --timer2Counter;
            if (timer2Counter == 0) interruptFlagRegister |= IFR_T2;
        }
    }

    // Shift register
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

    bool irq = (interruptFlagRegister & interruptEnableRegister & 0x7F) != 0;
    if (irq) interruptFlagRegister |= IFR_IRQ;
    else  interruptFlagRegister &= ~IFR_IRQ;
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
            return timer2Counter;
        }
        case 0x09:
        {
            return shiftRegister;
        }
        case 0x0A:
        {
            return auxiliaryControlRegister;
        }
        case 0x0B:
        {
            return peripheralControlRegister;
        }
        case 0x0C:
        {
            return interruptFlagRegister;
        }
        case 0x0D:
        {
            return interruptEnableRegister;
        }
        case 0x0E:
        {
            return reserved1;
        }
        case 0x0F:
        {
            return reserved2;
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
            timer2Counter = value;
            break;
        }
        case 0x09:
        {
            shiftRegister = value;
            break;
        }
        case 0x0A:
        {
            auxiliaryControlRegister = value;
            break;
        }
        case 0x0B:
        {
            peripheralControlRegister = value;
            break;
        }
        case 0x0C:
        {
            interruptFlagRegister &= ~value;
            break;
        }
        case 0x0D:
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
        case 0x0E:
        {
            reserved1 = value;
            break;
        }
        case 0x0F:
        {
            reserved2 = value;
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
