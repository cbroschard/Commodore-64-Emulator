// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Peripheral.h"
#include "IEC/IECBus.h"

Peripheral::Peripheral() :
    iecBus(nullptr),
    deviceNumber(-1)
{

}

Peripheral::~Peripheral() = default;

void Peripheral::attachIECBusInstance(IECBus* iecBus)
{
    this->iecBus = iecBus;
}

void Peripheral::detachIECBusInstance()
{
    IECBus* bus = iecBus;

    if (!bus)
        return;

    bus->unregisterDevice(deviceNumber);
}

void Peripheral::clearIECBusInstance(IECBus* bus)
{
    if (iecBus == bus)
        iecBus = nullptr;
}
