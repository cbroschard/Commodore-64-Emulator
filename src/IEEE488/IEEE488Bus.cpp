// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "IEEE488/IEEE488Bus.h"
#include "IEEE488/IEEE488Device.h"

IEEE488Bus::IEEE488Bus() :
    dio(0xFF),
    dav(true),
    nrfd(true),
    ndac(true),
    atn(true),
    eoi(true),
    ifc(true),
    ren(true),
    srq(true)
{

}

IEEE488Bus::~IEEE488Bus() = default;

void IEEE488Bus::reset()
{
    for (auto& entry : devices)
    {
        entry.dio = 0xFF;

        entry.dav  = true;
        entry.nrfd = true;
        entry.ndac = true;
        entry.atn  = true;
        entry.eoi  = true;
        entry.ifc  = true;
        entry.ren  = true;
        entry.srq  = true;
    }

    recalcAndNotify();
}

void IEEE488Bus::tick(uint32_t cycles)
{
    for (uint32_t i = 0; i < cycles; ++i)
        dispatchPendingChanges();
}

bool IEEE488Bus::attachController(DeviceId id)
{
    if (isDeviceAttached(id))
        return false;

    AttachedDevice entry{};
    entry.id = id;
    entry.device = nullptr;

    devices.push_back(entry);
    recalcAndNotify();

    return true;
}

bool IEEE488Bus::attachDevice(DeviceId id, IEEE488Device* device)
{
    if (!device)
        return false;

    // Prevent duplicate IDs or device pointers.
    for (const auto& entry : devices)
    {
        if (entry.id == id || entry.device == device)
            return false;
    }

    // Add device with all electrical outputs released.
    AttachedDevice entry{};
    entry.id = id;
    entry.device = device;

    devices.push_back(entry);
    recalcAndNotify();

    return true;
}

void IEEE488Bus::detachDevice(DeviceId id)
{
    for (auto it = devices.begin(); it != devices.end(); ++it)
    {
        if (it->id == id)
        {
            devices.erase(it);
            recalcAndNotify();
            return;
        }
    }
}

bool IEEE488Bus::isDeviceAttached(DeviceId id) const
{
    for (const auto& entry : devices)
    {
        if (entry.id == id)
            return true;
    }

    return false;
}

bool IEEE488Bus::setSignal(DeviceId id, Signal signal, bool level)
{
    AttachedDevice* device = findDevice(id);

    if (!device)
        return false;

    switch (signal)
    {
        case Signal::DAV:
            device->dav = level;
            break;

        case Signal::NRFD:
            device->nrfd = level;
            break;

        case Signal::NDAC:
            device->ndac = level;
            break;

        case Signal::ATN:
            device->atn = level;
            break;

        case Signal::EOI:
            device->eoi = level;
            break;

        case Signal::IFC:
            device->ifc = level;
            break;

        case Signal::REN:
            device->ren = level;
            break;

        case Signal::SRQ:
            device->srq = level;
            break;

        default:
            return false;
    }

    recalcAndNotify();
    return true;
}

bool IEEE488Bus::getSignal(Signal signal) const
{
    switch (signal)
    {
        case Signal::DAV:
            return dav;

        case Signal::NRFD:
            return nrfd;

        case Signal::NDAC:
            return ndac;

        case Signal::ATN:
            return atn;

        case Signal::EOI:
            return eoi;

        case Signal::IFC:
            return ifc;

        case Signal::REN:
            return ren;

        case Signal::SRQ:
            return srq;
    }

    return true;
}

bool IEEE488Bus::setDIO(DeviceId id, uint8_t value)
{
    AttachedDevice* device = findDevice(id);

    if (!device)
        return false;

    device->dio = value;

    recalcAndNotify();
    return true;
}

uint8_t IEEE488Bus::getDIO() const
{
    return dio;
}

void IEEE488Bus::recalcAndNotify()
{
    const BusState oldState = captureState();

    resolveSignals();

    const BusState newState = captureState();

    if (oldState == newState)
        return;

    pendingTransitions.push_back({oldState, newState});
}

void IEEE488Bus::dispatchPendingChanges()
{
    // Prevent recursive notification dispatch.
    if (dispatchingChanges)
        return;

    dispatchingChanges = true;

    // Limit work per call to protect against faulty
    // hardware logic causing endless oscillation.
    constexpr size_t MAX_TRANSITIONS = 4096;
    size_t processed = 0;

    while (!pendingTransitions.empty() && processed < MAX_TRANSITIONS)
    {
        const BusTransition transition = pendingTransitions.front();

        pendingTransitions.pop_front();
        ++processed;

        // Snapshot participant IDs because callbacks may
        // attach or detach devices, modifying the vector.
        std::vector<DeviceId> recipients;
        recipients.reserve(devices.size());

        for (const auto& entry : devices)
        {
            if (entry.device)
                recipients.push_back(entry.id);
        }

        for (DeviceId id : recipients)
        {
            // Re-find the device in case an earlier
            // callback changed bus registrations.
            AttachedDevice* entry = findDevice(id);

            if (!entry || !entry->device)
                continue;

            entry->device->onBusChanged(transition.oldState, transition.newState);
        }
    }

    dispatchingChanges = false;
}

void IEEE488Bus::resolveSignals()
{
    dio  = 0xFF;

    dav  = true;
    nrfd = true;
    ndac = true;
    atn  = true;
    eoi  = true;
    ifc  = true;
    ren  = true;
    srq  = true;

    for (const auto& device : devices)
    {
        dio &= device.dio;

        dav  &= device.dav;
        nrfd &= device.nrfd;
        ndac &= device.ndac;
        atn  &= device.atn;
        eoi  &= device.eoi;
        ifc  &= device.ifc;
        ren  &= device.ren;
        srq  &= device.srq;
    }
}

IEEE488Bus::AttachedDevice* IEEE488Bus::findDevice(DeviceId id)
{
    for (auto& device : devices)
    {
        if (device.id == id)
            return &device;
    }

    return nullptr;
}

IEEE488Bus::BusState IEEE488Bus::captureState() const
{
    BusState state;

    state.dav = dav;
    state.nrfd = nrfd;
    state.ndac = ndac;
    state.atn = atn;
    state.eoi = eoi;
    state.ifc = ifc;
    state.ren = ren;
    state.srq = srq;
    state.dio = dio;

    return state;
}
