// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Drive/D2031VIA.h"

D2031VIA::D2031VIA() :
    ledOn(false),
    syncDetected(false),
    mechDataLatch(0xFF),
    mechBytePending(false)
{

}

D2031VIA::~D2031VIA()
{

}

void D2031VIA::reset()
{
    DriveVIA6522::reset();


}

uint8_t D2031VIA::readRegister(uint16_t address)
{
    address &= 0x0F;

    uint8_t timerValue = 0xFF;

    if (readTimerRegister(address, timerValue))
        return timerValue;

    uint8_t irqValue = 0xFF;

    if (readInterruptRegister(address, irqValue))
        return irqValue;

    uint8_t controlValue = 0xFF;

    if (readControlRegister(address, controlValue))
        return controlValue;

    uint8_t portValue = 0xFF;

    if (readCommonPortRegister(address, portValue))
        return portValue;

    uint8_t shiftValue = 0xFF;

    if (readCommonShiftRegister(address, shiftValue))
        return shiftValue;

    switch (address)
    {
        case 0x01: // ORA/IRA - Port A
        {

        }

        case 0x0F: // ORA/IRA no handshake
        {

        }

        default:
            return 0xFF;
    }
}

void D2031VIA::writeRegister(uint16_t address, uint8_t value)
{
    address &= 0x0F;

    if (writeTimerRegister(address, value))
        return;

    if (writeInterruptRegister(address, value))
        return;

    if (writeControlRegister(address, value))
        return;

    if (writeCommonPortRegister(address, value))
        return;

    if (writeCommonShiftRegister(address, value))
        return;

    switch (address)
    {
        case 0x0F: // ORA/IRA no handshake
        {
            const uint8_t oldValue = registers.oraIRA;

            writePortALatch(value);
            onPortAOutputChanged(oldValue, value);
            acknowledgePortA();

            break;
        }

        default:
            break;
    }
}

void D2031VIA::onAttachedToPeripheral()
{

}

void D2031VIA::onPCRChanged(uint8_t oldValue, uint8_t newValue)
{

}

uint8_t D2031VIA::samplePortAInputs()
{
    uint8_t pins = portAPins;

    if (viaRole == VIARole::VIA2_Mechanics)
        return mechDataLatch;

    if (viaRole != VIARole::VIA1_IEEE488Bus)
        return pins;

    return pins;
}

uint8_t D2031VIA::samplePortBInputs()
{
    uint8_t pins = portBPins;
    const uint8_t ddrB = registers.ddrB;

    return pins;
}

void D2031VIA::onPortAOutputChanged(uint8_t oldValue, uint8_t newValue)
{

}

void D2031VIA::onPortBOutputChanged(uint8_t oldValue, uint8_t newValue)
{

}

void D2031VIA::onDDRAChanged(uint8_t oldDDR, uint8_t newDDR)
{

}

void D2031VIA::onDDRBChanged(uint8_t oldDDR, uint8_t newDDR)
{

}

void D2031VIA::onShiftRegisterWritten()
{

}

uint8_t D2031VIA::sampleShiftRegisterRead()
{
    if (viaRole == VIARole::VIA2_Mechanics)
        return mechDataLatch;

    return registers.serialShift;
}
