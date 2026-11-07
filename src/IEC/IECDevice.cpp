// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "IEC/IECBus.h"
#include "IEC/IECDevice.h"

IECDevice::IECDevice() :
    iecBus(nullptr),
    assertClk(false),
    assertData(false),
    assertAtn(false),
    assertSrq(false),
    listening(false),
    talking(false),
    shiftReg(0),
    bitsProcessed(0),
    iecDeviceNumber(-1)
{

}

IECDevice::~IECDevice() = default;

void IECDevice::attachIECBusInstance(IECBus* bus)
{
    iecBus = bus;
}

void IECDevice::detachIECBusInstance()
{
    iecBus = nullptr;
}

uint8_t IECDevice::nextOutputByte()
{
    return 0xFF;
}

void IECDevice::peripheralAssertClk(bool state)
{
    if (assertClk == state)
        return;

    assertClk = state;

    if (iecBus)
        iecBus->peripheralControlClk(this, state);
}

void IECDevice::peripheralAssertData(bool state)
{
    if (assertData == state)
        return;

    assertData = state;

    if (iecBus)
        iecBus->peripheralControlData(this, state);
}

void IECDevice::peripheralAssertAtn(bool state)
{
    if (assertAtn == state)
        return;

    assertAtn = state;

    if (iecBus)
        iecBus->peripheralControlAtn(this, state);
}

void IECDevice::peripheralAssertSrq(bool state)
{
    if (assertSrq == state)
        return;

    assertSrq = state;

    if (iecBus)
        iecBus->peripheralControlSrq(this, state);
}
