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

    resolveSignals();
}

bool IEEE488Bus::attachController(DeviceId id)
{
    if (isDeviceAttached(id))
        return false;

    AttachedDevice entry{};
    entry.id = id;
    entry.device = nullptr;

    devices.push_back(entry);
    resolveSignals();

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
    resolveSignals();

    return true;
}

void IEEE488Bus::detachDevice(DeviceId id)
{
    for (auto it = devices.begin(); it != devices.end(); ++it)
    {
        if (it->id == id)
        {
            devices.erase(it);
            resolveSignals();
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

    resolveSignals();
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

    resolveSignals();
    return true;
}

uint8_t IEEE488Bus::getDIO() const
{
    return dio;
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
