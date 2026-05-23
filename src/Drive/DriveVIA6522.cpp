// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Drive/DriveVIA6522.h"

DriveVIA6522::DriveVIA6522(DriveVIA6522::VIARole role) :
    role(role),
    timer1Counter(0),
    timer1Latch(0),
    timer1Running(false),
    timer1JustLoaded(false),
    timer1ReloadPending(false),
    timer1InhibitIRQ(false),
    timer1PB7Level(true),
    timer2Counter(0),
    timer2Latch(0),
    timer2Running(false),
    timer2JustLoaded(false),
    timer2LowLatchByte(0x00)
{

}

DriveVIA6522::~DriveVIA6522() = default;

void DriveVIA6522::reset()
{
    // Pin defaults
    portAPins                           = 0xFF;
    portBPins                           = 0xFF;

    // Initialize registers
    registers.orbIRB                    = 0x00;
    registers.oraIRA                    = 0x00;
    registers.ddrB                      = 0x00;
    registers.ddrA                      = 0x00;
    registers.timer1CounterLowByte      = 0x00;
    registers.timer1CounterHighByte     = 0x00;
    registers.timer1LowLatch            = 0x00;
    registers.timer1HighLatch           = 0x00;
    registers.timer2CounterLowByte      = 0x00;
    registers.timer2CounterHighByte     = 0x00;
    registers.serialShift               = 0x00;
    registers.auxControlRegister        = 0x00;
    registers.peripheralControlRegister = 0x00;
    registers.interruptFlag             = 0x00;
    registers.interruptEnable           = 0x00;
    registers.oraIRANoHandshake         = 0x00;

    // Timer defaults
    timer1Counter                       = 0;
    timer1Latch                         = 0;
    timer1Running                       = false;
    timer1JustLoaded                    = false;
    timer1InhibitIRQ                    = false;
    timer1PB7Level                      = true;

    timer2Counter                       = 0;
    timer2Latch                         = 0;
    timer2Running                       = false;
    timer2JustLoaded                    = false;
    timer2InhibitIRQ                    = false;
    timer2LowLatchByte                  = 0x00;
}

void DriveVIA6522::tick(uint32_t cycles)
{
    while (cycles-- > 0)
    {
        timer1Tick();
        timer2Tick();
    }

}

bool DriveVIA6522::checkIRQActive() const
{
    uint8_t active = registers.interruptEnable & registers.interruptFlag & 0x7F;
    return active != 0;
}

void DriveVIA6522::triggerInterrupt(uint8_t sourceMask)
{
    registers.interruptFlag |= sourceMask;
    refreshMasterBit();
}

void DriveVIA6522::clearIFR(uint8_t sourceMask)
{
    registers.interruptFlag &= static_cast<uint8_t>(~sourceMask);
    refreshMasterBit();
}

void DriveVIA6522::refreshMasterBit()
{
    const uint8_t pendingEnabled = registers.interruptFlag & registers.interruptEnable & 0x7F;

    if (pendingEnabled)
        registers.interruptFlag |= IFR_IRQ;
    else
        registers.interruptFlag &= static_cast<uint8_t>(~IFR_IRQ);
}

void DriveVIA6522::timer1Tick()
{
    if (!timer1Running)
        return;

    if (timer1ReloadPending)
    {
        timer1Counter = timer1Latch;
        timer1ReloadPending = false;
        timer1JustLoaded = true;
        syncTimer1Registers();
    }

    if (timer1JustLoaded)
    {
        timer1JustLoaded = false;
        return;
    }

    timer1Counter = static_cast<uint16_t>(timer1Counter - 1);
    syncTimer1Registers();

    if (timer1Counter == 0x0000 && !timer1InhibitIRQ)
    {
        triggerInterrupt(IFR_TIMER1);

        const bool freeRun =
            (registers.auxControlRegister & ACR_T1_CONTINUOUS) != 0;

        if (freeRun)
        {
            timer1ReloadPending = true;
        }
        else
        {
            timer1InhibitIRQ = true;
        }
    }
}

void DriveVIA6522::timer2Tick()
{
    if (!timer2Running)
        return;

    const bool pulseCountMode =
        (registers.auxControlRegister & ACR_T2_PULSE_COUNT) != 0;

    if (pulseCountMode)
        return;

    if (timer2JustLoaded)
    {
        timer2JustLoaded = false;
        return;
    }

    timer2Counter = static_cast<uint16_t>(timer2Counter - 1);
    syncTimer2Registers();

    if (timer2Counter == 0x0000 && !timer2InhibitIRQ)
    {
        triggerInterrupt(IFR_TIMER2);
        timer2InhibitIRQ = true;
    }
}

void DriveVIA6522::syncTimer1Registers()
{
    registers.timer1CounterLowByte =
        static_cast<uint8_t>(timer1Counter & 0xFF);

    registers.timer1CounterHighByte =
        static_cast<uint8_t>((timer1Counter >> 8) & 0xFF);
}

void DriveVIA6522::syncTimer2Registers()
{
    registers.timer2CounterLowByte =
        static_cast<uint8_t>(timer2Counter & 0xFF);

    registers.timer2CounterHighByte =
        static_cast<uint8_t>((timer2Counter >> 8) & 0xFF);
}
