// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "IEEE488/IEEE488Device.h"

IEEE488Device::IEEE488Device() :
    ieee488Bus(nullptr),
    participantId(0)
{

}

IEEE488Device::~IEEE488Device()
{
    if (ieee488Bus)
        detachBus();

    ieee488Bus = nullptr;
}

void IEEE488Device::reset()
{

}

bool IEEE488Device::attachBus(IEEE488Bus* bus, IEEE488Bus::DeviceId id)
{

}

void IEEE488Device::detachBus()
{

}

bool IEEE488Device::isConnected() const
{

}

bool IEEE488Device::readSignal(IEEE488Bus::Signal signal) const
{

}

uint8_t IEEE488Device::readDIO() const
{

}

bool IEEE488Device::driveSignal(IEEE488Bus::Signal signal, bool level)
{

}

bool IEEE488Device::driveDIO(uint8_t value)
{

}

void IEEE488Device::releaseBusLines()
{

}
