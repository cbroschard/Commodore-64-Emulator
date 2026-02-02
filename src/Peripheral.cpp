// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Peripheral.h"
#include "IECBUS.h"

Peripheral::Peripheral() :
    bus(nullptr),
    deviceNumber(-1),
    assertClk(false),
    assertData(false),
    assertAtn(false),
    assertSrq(false),
    listening(false),
    talking(false),
    shiftReg(0),
    bitsProcessed(0)
{

}

Peripheral::~Peripheral() = default;

void Peripheral::attachBusInstance(IECBUS* bus)
{
    this->bus = bus;
}

void Peripheral::detachBusInstance()
{
    if (bus)
    {
        bus->unregisterDevice(deviceNumber);
        bus = nullptr;
    }
}

void Peripheral::peripheralAssertClk(bool state)
{
    if (assertClk == state) return;

    assertClk = state;

    if (bus) bus->peripheralControlClk(this, state);
}

void Peripheral::peripheralAssertData(bool state)
{
    if (assertData == state) return;

    assertData = state;

    if (bus) bus->peripheralControlData(this, state);
}

void Peripheral::peripheralAssertAtn(bool state)
{
    if (bus) bus->peripheralControlAtn(this, state);
}

void Peripheral::peripheralAssertSrq(bool state)
{
    if (bus) bus->peripheralControlSrq(this, state);
}

uint8_t Peripheral::nextOutputByte()
{
    return 0xFF; // Overriden by derived device
}
