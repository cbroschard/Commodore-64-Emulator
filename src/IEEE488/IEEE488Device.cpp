// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include <utility>
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
}

void IEEE488Device::setBusChangeCallback(BusChangeCallback callback)
{
    busChangeCallback = std::move(callback);
}

void IEEE488Device::reset()
{
    releaseBusLines();
}

bool IEEE488Device::attachBus(IEEE488Bus* bus, IEEE488Bus::DeviceId id)
{
    if (!bus)
        return false;

    if (ieee488Bus == bus)
        return participantId == id;

    if (!bus->attachDevice(id, this))
        return false;

    if (ieee488Bus)
        ieee488Bus->detachDevice(participantId);

    ieee488Bus = bus;
    participantId = id;

    return true;
}

void IEEE488Device::detachBus()
{
    if (!ieee488Bus)
        return;

    // Unregister from the bus.
    ieee488Bus->detachDevice(participantId);

    // Clear the connection.
    ieee488Bus = nullptr;
    participantId = 0;
}

bool IEEE488Device::isConnected() const
{
    return ieee488Bus != nullptr;
}

void IEEE488Device::onBusChanged(const IEEE488Bus::BusState& oldState, const IEEE488Bus::BusState& newState)
{
    if (busChangeCallback)
        busChangeCallback(oldState, newState);
}

bool IEEE488Device::readSignal(IEEE488Bus::Signal signal) const
{
    if (!ieee488Bus)
        return true;

    return ieee488Bus->getSignal(signal);
}

uint8_t IEEE488Device::readDIO() const
{
    if (!ieee488Bus)
        return 0xFF;

    return ieee488Bus->getDIO();
}

bool IEEE488Device::driveSignal(IEEE488Bus::Signal signal, bool level)
{
    if (!ieee488Bus)
        return false;

    if (!ieee488Bus->setSignal(participantId, signal, level))
        return false;

    return true;
}

bool IEEE488Device::driveDIO(uint8_t value)
{
    if (!ieee488Bus)
        return false;

    if (!ieee488Bus->setDIO(participantId, value))
        return false;

    return true;
}


void IEEE488Device::releaseBusLines()
{
    if (!ieee488Bus)
        return;

    // Release all eight data lines.
    driveDIO(0xFF);

    // Release all IEEE-488 control lines.
    driveSignal(IEEE488Bus::Signal::DAV,  true);
    driveSignal(IEEE488Bus::Signal::NRFD, true);
    driveSignal(IEEE488Bus::Signal::NDAC, true);
    driveSignal(IEEE488Bus::Signal::ATN,  true);
    driveSignal(IEEE488Bus::Signal::EOI,  true);
    driveSignal(IEEE488Bus::Signal::IFC,  true);
    driveSignal(IEEE488Bus::Signal::REN,  true);
    driveSignal(IEEE488Bus::Signal::SRQ,  true);
}
