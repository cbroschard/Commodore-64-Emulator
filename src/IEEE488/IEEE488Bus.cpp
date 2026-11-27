// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include <algorithm>
#include <deque>
#include <vector>
#include "Drive/Drive.h"
#include "IEEE488/IEEE488Bus.h"
#include "IEEE488/IEEE488Device.h"
#include "Debug/MLMonitor.h"
#include "Debug/MLMonitorBackend.h"

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

void IEEE488Bus::saveState(StateWriter& wrtr) const
{
    // -----------------------------------------
    // IEEE-488 participants
    // -----------------------------------------
    wrtr.writeU32(static_cast<uint32_t>(devices.size()));

    for (const auto& device : devices)
    {
        // Participant identity
        wrtr.writeU32(device.id);

        // Participant type
        wrtr.writeBool(device.device != nullptr);

        // Data bus output
        wrtr.writeU8(device.dio);

        // Control-line outputs
        wrtr.writeBool(device.dav);
        wrtr.writeBool(device.nrfd);
        wrtr.writeBool(device.ndac);
        wrtr.writeBool(device.atn);
        wrtr.writeBool(device.eoi);
        wrtr.writeBool(device.ifc);
        wrtr.writeBool(device.ren);
        wrtr.writeBool(device.srq);
    }

    // -----------------------------------------
    // Pending bus transitions
    // -----------------------------------------
    wrtr.writeU32(
        static_cast<uint32_t>(pendingTransitions.size())
    );

    for (const auto& transition : pendingTransitions)
    {
        const BusState& oldState = transition.oldState;
        const BusState& newState = transition.newState;

        // Previous bus state
        wrtr.writeBool(oldState.dav);
        wrtr.writeBool(oldState.nrfd);
        wrtr.writeBool(oldState.ndac);
        wrtr.writeBool(oldState.atn);
        wrtr.writeBool(oldState.eoi);
        wrtr.writeBool(oldState.ifc);
        wrtr.writeBool(oldState.ren);
        wrtr.writeBool(oldState.srq);
        wrtr.writeU8(oldState.dio);

        // New bus state
        wrtr.writeBool(newState.dav);
        wrtr.writeBool(newState.nrfd);
        wrtr.writeBool(newState.ndac);
        wrtr.writeBool(newState.atn);
        wrtr.writeBool(newState.eoi);
        wrtr.writeBool(newState.ifc);
        wrtr.writeBool(newState.ren);
        wrtr.writeBool(newState.srq);
        wrtr.writeU8(newState.dio);
    }
}

bool IEEE488Bus::loadState(StateReader& rdr)
{
    // Do not restore while dispatching bus callbacks.
    if (dispatchingChanges)
        return false;

    // -----------------------------------------
    // Read participant count
    // -----------------------------------------
    uint32_t deviceCount = 0;

    if (!rdr.readU32(deviceCount))
        return false;

    // Protect against corrupted save-state data.
    if (deviceCount > 64)
        return false;

    // Saved participant outputs.
    struct SavedDevice
    {
        DeviceId id = 0;
        bool peripheral = false;

        uint8_t dio = 0xFF;

        bool dav = true;
        bool nrfd = true;
        bool ndac = true;
        bool atn = true;
        bool eoi = true;
        bool ifc = true;
        bool ren = true;
        bool srq = true;
    };

    std::vector<SavedDevice> savedDevices;
    savedDevices.reserve(deviceCount);

    // -----------------------------------------
    // Read participant outputs
    // -----------------------------------------
    for (uint32_t i = 0; i < deviceCount; ++i)
    {
        SavedDevice saved;

        if (!rdr.readU32(saved.id))
            return false;

        if (!rdr.readBool(saved.peripheral))
            return false;

        if (!rdr.readU8(saved.dio))
            return false;

        if (!rdr.readBool(saved.dav))
            return false;

        if (!rdr.readBool(saved.nrfd))
            return false;

        if (!rdr.readBool(saved.ndac))
            return false;

        if (!rdr.readBool(saved.atn))
            return false;

        if (!rdr.readBool(saved.eoi))
            return false;

        if (!rdr.readBool(saved.ifc))
            return false;

        if (!rdr.readBool(saved.ren))
            return false;

        if (!rdr.readBool(saved.srq))
            return false;

        savedDevices.push_back(saved);
    }

    // -----------------------------------------
    // Validate participant configuration
    // -----------------------------------------

    // All saved participants must exist in the
    // currently attached bus.
    if (savedDevices.size() != devices.size())
        return false;

    for (size_t i = 0; i < savedDevices.size(); ++i)
    {
        const SavedDevice& saved = savedDevices[i];

        // Reject duplicate participant IDs.
        for (size_t j = 0; j < i; ++j)
        {
            if (savedDevices[j].id == saved.id)
                return false;
        }

        const auto it = std::find_if(
            devices.begin(),
            devices.end(),
            [&saved](const AttachedDevice& device)
            {
                return device.id == saved.id;
            }
        );

        if (it == devices.end())
            return false;

        // Verify controller/peripheral identity.
        if ((it->device != nullptr) != saved.peripheral)
            return false;
    }

    // -----------------------------------------
    // Read pending transitions
    // -----------------------------------------
    uint32_t transitionCount = 0;

    if (!rdr.readU32(transitionCount))
        return false;

    // Match the existing dispatch safety limit.
    if (transitionCount > 4096)
        return false;

    std::deque<BusTransition> restoredTransitions;

    for (uint32_t i = 0; i < transitionCount; ++i)
    {
        BusTransition transition;

        BusState& oldState = transition.oldState;
        BusState& newState = transition.newState;

        // Previous bus state
        if (!rdr.readBool(oldState.dav))
            return false;

        if (!rdr.readBool(oldState.nrfd))
            return false;

        if (!rdr.readBool(oldState.ndac))
            return false;

        if (!rdr.readBool(oldState.atn))
            return false;

        if (!rdr.readBool(oldState.eoi))
            return false;

        if (!rdr.readBool(oldState.ifc))
            return false;

        if (!rdr.readBool(oldState.ren))
            return false;

        if (!rdr.readBool(oldState.srq))
            return false;

        if (!rdr.readU8(oldState.dio))
            return false;

        // New bus state
        if (!rdr.readBool(newState.dav))
            return false;

        if (!rdr.readBool(newState.nrfd))
            return false;

        if (!rdr.readBool(newState.ndac))
            return false;

        if (!rdr.readBool(newState.atn))
            return false;

        if (!rdr.readBool(newState.eoi))
            return false;

        if (!rdr.readBool(newState.ifc))
            return false;

        if (!rdr.readBool(newState.ren))
            return false;

        if (!rdr.readBool(newState.srq))
            return false;

        if (!rdr.readU8(newState.dio))
            return false;

        restoredTransitions.push_back(transition);
    }

    // -----------------------------------------
    // Apply restored participant outputs
    // -----------------------------------------
    for (const SavedDevice& saved : savedDevices)
    {
        AttachedDevice* device = findDevice(saved.id);

        // Already validated above.
        if (!device)
            return false;

        device->dio = saved.dio;

        device->dav = saved.dav;
        device->nrfd = saved.nrfd;
        device->ndac = saved.ndac;
        device->atn = saved.atn;
        device->eoi = saved.eoi;
        device->ifc = saved.ifc;
        device->ren = saved.ren;
        device->srq = saved.srq;
    }

    // -----------------------------------------
    // Restore pending transitions
    // -----------------------------------------
    pendingTransitions = std::move(restoredTransitions);

    // -----------------------------------------
    // Recalculate physical bus levels
    // -----------------------------------------
    // This does not generate callbacks or queue
    // additional transitions.
    resolveSignalsWithoutNotification();

    dispatchingChanges = false;

    return true;
}

void IEEE488Bus::reset()
{
    pendingTransitions.clear();

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
    {
        // Process controller or device changes from the previous cycle.
        dispatchPendingChanges();

        // Snapshot IDs to avoid holding references into the vector
        // while drive callbacks are executing.
        std::vector<DeviceId> driveIds;

        for (const auto& entry : devices)
        {
            if (entry.drive)
                driveIds.push_back(entry.id);
        }

        // Execute each attached drive.
        for (DeviceId id : driveIds)
        {
            auto* entry = findDevice(id);

            if (!entry || !entry->drive)
                continue;

            Drive* drive = entry->drive;

            drive->tick(1);

            // Deliver transitions generated during drive execution.
            dispatchPendingChanges();
        }

        // Process any remaining transitions.
        dispatchPendingChanges();
    }
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

bool IEEE488Bus::registerDrive(DeviceId id, Drive* drive)
{
    if (!drive)
        return false;

    auto* entry = findDevice(id);

    if (!entry || !entry->device)
        return false;

    if (entry->drive && entry->drive != drive)
        return false;

    entry->drive = drive;
    return true;
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

std::vector<IEEE488Bus::RegisteredDrive> IEEE488Bus::getRegisteredDrives() const
{
    std::vector<RegisteredDrive> result;

    for (const auto& entry : devices)
    {
        if (!entry.drive)
            continue;

        result.push_back({entry.id, entry.drive});
    }

    return result;
}

IEEE488Bus::PhysicalSnapshot IEEE488Bus::snapshotPhysical() const
{
    PhysicalSnapshot snapshot{};

    snapshot.atn  = atn;
    snapshot.dav  = dav;
    snapshot.nrfd = nrfd;
    snapshot.ndac = ndac;
    snapshot.eoi  = eoi;
    snapshot.ifc  = ifc;
    snapshot.ren  = ren;
    snapshot.srq  = srq;
    snapshot.dio  = dio;

    snapshot.pendingTransitionCount = pendingTransitions.size();

    for (const auto& entry : devices)
    {
        if (entry.device)
            ++snapshot.deviceCount;
        else
            ++snapshot.controllerCount;

        if (entry.drive)
            ++snapshot.driveCount;
    }

    return snapshot;
}

std::string IEEE488Bus::debugPhysicalSnapshotString() const
{
    const auto s = snapshotPhysical();

    std::ostringstream out;

    out << "IEEE-488 Physical Bus:\n";

    out << "  ATN="  << (s.atn  ? "H" : "L")
        << " DAV="   << (s.dav  ? "H" : "L")
        << " NRFD="  << (s.nrfd ? "H" : "L")
        << " NDAC="  << (s.ndac ? "H" : "L")
        << " EOI="   << (s.eoi  ? "H" : "L")
        << "\n";

    out << "  IFC="  << (s.ifc ? "H" : "L")
        << " REN="   << (s.ren ? "H" : "L")
        << " SRQ="   << (s.srq ? "H" : "L")
        << "\n";

    out << "  DIO=$"
        << std::hex << std::uppercase
        << std::setw(2) << std::setfill('0')
        << static_cast<unsigned>(s.dio)
        << std::dec << "\n";

    out << "  Controllers: " << s.controllerCount << "\n";
    out << "  Devices: " << s.deviceCount << "\n";
    out << "  Drives: " << s.driveCount << "\n";
    out << "  Pending transitions: "
        << s.pendingTransitionCount << "\n";

    out << "\nIEEE-488 Registered Participants:\n";

    for (const auto& entry : devices)
    {
        out << "  Bus ID: " << entry.id
            << "  Type: "
            << (entry.device ? "Peripheral" : "Controller")
            << "  Drive: "
            << (entry.drive ? "Yes" : "No");

        if (entry.drive)
        {
            out << "  Drive device number: "
                << entry.drive->getDeviceNumber();
        }

        out << " ATN=" << (entry.atn ? "H" : "L")
            << " DAV=" << (entry.dav ? "H" : "L")
            << " NRFD=" << (entry.nrfd ? "H" : "L")
            << " NDAC=" << (entry.ndac ? "H" : "L")
            << " DIO=$"
            << std::hex << std::uppercase
            << std::setw(2) << std::setfill('0')
            << static_cast<unsigned>(entry.dio)
            << std::dec;

        out << '\n';
    }

    return out.str();
}

Drive* IEEE488Bus::getDrive(DeviceId id) const
{
    for (const auto& entry : devices)
    {
        if (entry.id == id)
            return entry.drive;
    }

    return nullptr;
}

Drive* IEEE488Bus::getDriveWithBreakpointHit() const
{
    for (const auto& entry : devices)
    {
        if (entry.drive && entry.drive->isBreakpointHit())
            return entry.drive;
    }

    return nullptr;
}

Drive* IEEE488Bus::getDriveWithWatchpointHit() const
{
    for (const auto& entry : devices)
    {
        if (entry.drive && entry.drive->isWatchpointHit())
            return entry.drive;
    }

    return nullptr;
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

void IEEE488Bus::resolveSignalsWithoutNotification()
{
    resolveSignals();
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
