// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Drive/DriveVIA6522.h"

DriveVIA6522::DriveVIA6522()
{

}

DriveVIA6522::~DriveVIA6522()
{

}

void DriveVIA6522::reset()
{
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
}

void DriveVIA6522::tick(uint32_t cycles)
{

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
