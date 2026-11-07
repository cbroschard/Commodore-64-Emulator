// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "CIA2.h"
#include "Drive/Drive.h"
#include "IEC/IECBus.h"
#include "IEC/IECDevice.h"
#include "IEC/IECDriveDiagnostics.h"

IECBus::IECBus() :
    //Initialize to defaults
    currentState(State::IDLE),
    cia2(nullptr),
    currentTalker(nullptr),
    hostCpuHz(1022727.0),
    line_srqin(true),
    c64DrivesAtnLow(false),
    c64DrivesClkLow(false),
    c64DrivesDataLow(false),
    peripheralDrivesClkLow(false),
    peripheralDrivesDataLow(false),
    peripheralDrivesAtnLow(false),
    lastClk(true),
    romControlledIEC(false),
    traceWriteIndex(0),
    traceCount(0)
{
    currentListeners.clear(); // Ensure listener list is empty on start
    reset();
}

IECBus::~IECBus() = default;

void IECBus::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("IEC0");
    wrtr.writeU32(2); //version

    wrtr.writeU8(static_cast<uint8_t>(currentState));

    wrtr.writeBool(c64DrivesAtnLow);
    wrtr.writeBool(c64DrivesClkLow);
    wrtr.writeBool(c64DrivesDataLow);

    wrtr.writeBool(lastClk);

    // Talker id (device number)
    int32_t talkerId = -1;

    if (currentTalker)
        talkerId = static_cast<int32_t>(findDeviceNumber(currentTalker));

    wrtr.writeI32(talkerId);

    // Listeners
    wrtr.writeU8(static_cast<uint8_t>(currentListeners.size()));

    for (Peripheral* peripheral : currentListeners)
    {
        const int32_t id = static_cast<int32_t>(findDeviceNumber(peripheral));
        wrtr.writeI32(id);
    }

    // Preserve the legacy contribution table in the v2 format.
    // IECDevice is authoritative, so legacy contributions are always false.
    wrtr.writeU8(static_cast<uint8_t>(registeredDevices.size()));

    for (const auto& [num, registration] : registeredDevices)
    {
        wrtr.writeI32(static_cast<int32_t>(num));
        wrtr.writeBool(false);
        wrtr.writeBool(false);
        wrtr.writeBool(false);
    }

    wrtr.writeBool(romControlledIEC);

    // Version 2: IECDevice electrical contributions.
    // Store device numbers, never raw pointers.
    uint8_t iecCount = 0;

    for (const auto& [num, registration] : registeredDevices)
    {
        if (registration.iecDevice)
            ++iecCount;
    }

    wrtr.writeU8(iecCount);

    for (const auto& [num, registration] : registeredDevices)
    {
        IECDevice* device = registration.iecDevice;

        if (!device)
            continue;

        wrtr.writeI32(static_cast<int32_t>(num));

        const auto clk = iecDrivesClkLow.find(device);
        const auto data = iecDrivesDataLow.find(device);
        const auto atn = iecDrivesAtnLow.find(device);

        wrtr.writeBool(
            clk != iecDrivesClkLow.end() && clk->second
        );

        wrtr.writeBool(
            data != iecDrivesDataLow.end() && data->second
        );

        wrtr.writeBool(
            atn != iecDrivesAtnLow.end() && atn->second
        );
    }

    wrtr.endChunk();
}

bool IECBus::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "IEC0", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    uint32_t ver = 0;
    if (!rdr.readU32(ver))                                          { rdr.exitChunkPayload(chunk); return false; }
    if (ver != 1 && ver != 2)                                       { rdr.exitChunkPayload(chunk); return false; }

    uint8_t st = 0;
    if (!rdr.readU8(st))                                            { rdr.exitChunkPayload(chunk); return false; }
    currentState = static_cast<State>(st);

    if (!rdr.readBool(c64DrivesAtnLow))                             { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(c64DrivesClkLow))                             { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(c64DrivesDataLow))                            { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readBool(lastClk))                                     { rdr.exitChunkPayload(chunk); return false; }

    int32_t talkerId = -1;
    if (!rdr.readI32(talkerId))                                     { rdr.exitChunkPayload(chunk); return false; }

    // listeners
    uint8_t listenerCount = 0;
    if (!rdr.readU8(listenerCount))                                 { rdr.exitChunkPayload(chunk); return false; }

    std::vector<int32_t> listenerIds;
    listenerIds.reserve(listenerCount);
    for (uint8_t i = 0; i < listenerCount; ++i)
    {
        int32_t id = -1;
        if (!rdr.readI32(id))                                       { rdr.exitChunkPayload(chunk); return false; }
        listenerIds.push_back(id);
    }

    // device contribution table
    uint8_t devCount = 0;
    if (!rdr.readU8(devCount))                                      { rdr.exitChunkPayload(chunk); return false; }

    // Clear current contribution maps
    iecDrivesClkLow.clear();
    iecDrivesDataLow.clear();
    iecDrivesAtnLow.clear();

    for (uint8_t i = 0; i < devCount; ++i)
    {
        int32_t id = -1;
        bool clkLow=false, dataLow=false, atnLow=false;

        if (!rdr.readI32(id))                                       { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(clkLow))                                  { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(dataLow))                                 { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(atnLow))                                  { rdr.exitChunkPayload(chunk); return false; }
    }

    if (!rdr.readBool(romControlledIEC))                            { rdr.exitChunkPayload(chunk); return false; }

    // Version 2: Restore IECDevice electrical contributions.
    if (ver >= 2)
    {
        uint8_t iecCount = 0;

        if (!rdr.readU8(iecCount))
        {
            rdr.exitChunkPayload(chunk);
            return false;
        }

        for (uint16_t i = 0; i < iecCount; ++i)
        {
            int32_t id = -1;

            bool clkLow = false;
            bool dataLow = false;
            bool atnLow = false;

            if (!rdr.readI32(id) || !rdr.readBool(clkLow) || !rdr.readBool(dataLow) || !rdr.readBool(atnLow))
            {
                rdr.exitChunkPayload(chunk);
                return false;
            }

            const RegisteredDevice* registration = findRegisteredDevice(static_cast<int>(id));

            if (!registration || !registration->iecDevice)
                continue;

            IECDevice* device = registration->iecDevice;

            iecDrivesClkLow[device] = clkLow;
            iecDrivesDataLow[device] = dataLow;
            iecDrivesAtnLow[device] = atnLow;
        }
    }

    // Restore talker/listeners pointers from IDs
    currentTalker = nullptr;

    if (talkerId >= 0)
    {
        const RegisteredDevice* registration = findRegisteredDevice(static_cast<int>(talkerId));

        if (registration)
            currentTalker = registration->peripheral;
    }

    currentListeners.clear();

    for (int32_t id : listenerIds)
    {
        const RegisteredDevice* registration = findRegisteredDevice(static_cast<int>(id));

        if (registration && registration->peripheral)
            currentListeners.push_back(registration->peripheral);
    }

    // Synchronize IECDevice internal assertion flags with
    // the electrical contributions restored from the snapshot.
    for (const auto& [num, registration] : registeredDevices)
    {
        IECDevice* device = registration.iecDevice;

        if (!device)
            continue;

        const auto clk = iecDrivesClkLow.find(device);
        const auto data = iecDrivesDataLow.find(device);
        const auto atn = iecDrivesAtnLow.find(device);

        device->assertClk = clk != iecDrivesClkLow.end() && clk->second;
        device->assertData = data != iecDrivesDataLow.end() && data->second;
        device->assertAtn = atn != iecDrivesAtnLow.end() && atn->second;
    }

    // Re-resolve and notify everyone
    updateSrqLine();
    recalcAndNotify();

    rdr.exitChunkPayload(chunk);

    return true;
}

void IECBus::reset()
{
    // Reset bus-level state machine
    currentState = State::IDLE;
    currentTalker = nullptr;
    currentListeners.clear();

    // Release C64-driven open-collector lines (idle = HIGH)
    c64DrivesAtnLow  = false;
    c64DrivesClkLow  = false;
    c64DrivesDataLow = false;

    // Release SRQ (idle = HIGH)
    line_srqin = true;

    // Clear all device pull-low contributions
    iecDrivesClkLow.clear();
    iecDrivesDataLow.clear();
    iecDrivesAtnLow.clear();

    // Synchronize IECDevice assertion flags with the cleared maps.
    for (const auto& [num, registration] : registeredDevices)
    {
        if (IECDevice* device = registration.iecDevice)
        {
            device->assertClk  = false;
            device->assertData = false;
            device->assertAtn  = false;
        }
    }

    // Recompute resolved line levels (idle = HIGH)
    updateBusState();

    // Capture resolved bus levels
    const bool atnLow  = !busLines.atn;
    const bool clkLow  = !busLines.clk;
    const bool dataLow = !busLines.data;

    // Reset compatibility mode
    romControlledIEC = false;

    // Synchronize CIA2 first
    if (cia2)
    {
        cia2->atnChanged(atnLow);
        cia2->clkChanged(clkLow);
        cia2->dataChanged(dataLow);
        cia2->srqChanged(line_srqin);
    }

    // Notify all registered IEC devices exactly once
    for (const auto& [num, registration] : registeredDevices)
    {
        if (IECDevice* device = registration.iecDevice)
        {
            device->atnChanged(atnLow);
            device->clkChanged(clkLow);
            device->dataChanged(dataLow);
        }
    }

    // Reset drive timing accumulators
    for (auto& [dev, acc] : driveCycleAccumulators)
        acc = 0.0;

    lastClk = busLines.clk;

    clearTraceHistory();
}

void IECBus::setAtnLine(bool state)
{
    c64DrivesAtnLow = !state;
    currentState = c64DrivesAtnLow ? State::ATTENTION : State::IDLE;

    recalcAndNotify();
}

void IECBus::setClkLine(bool state)
{
    c64DrivesClkLow = !state;
    recalcAndNotify();
}

void IECBus::setDataLine(bool state)
{
    c64DrivesDataLow = !state;
    recalcAndNotify();
}

void IECBus::setSrqLine(bool state)
{
    bool old = line_srqin;
    line_srqin = state;
    updateBusState();
    // Only notify CIA2 if it actually changed
    if (old != line_srqin && cia2) cia2->srqChanged(line_srqin);
}

void IECBus::setC64IECOutputs(bool atnReleased, bool clkReleased, bool dataReleased)
{
    c64DrivesAtnLow  = !atnReleased;
    c64DrivesClkLow  = !clkReleased;
    c64DrivesDataLow = !dataReleased;

    currentState = c64DrivesAtnLow ? State::ATTENTION : State::IDLE;

    recalcAndNotify();
}

void IECBus::peripheralControlClk(IECDevice* device, bool clkLow)
{
    if (!device)
        return;

    const RegisteredDevice* registration = findRegisteredDevice(device->getIECDeviceNumber());

    if (!registration || registration->iecDevice != device)
        return;

    const bool old = iecDrivesClkLow[device];

    if (old == clkLow)
        return;

    iecDrivesClkLow[device] = clkLow;

    recalcAndNotify();

    uint64_t cycle = 0;
    uint16_t pc = 0;

    if (Peripheral* peripheral = registration->peripheral)
    {
        if (Drive* drive = peripheral->asDrive())
        {
            if (const CPU* cpu = drive->getDriveCPU())
            {
                cycle = cpu->getTotalCycles();
                pc = cpu->getPC();
            }
        }
    }

    recordTrace(TraceType::PeripheralClk, cycle, pc, device->getIECDeviceNumber(), static_cast<uint8_t>(clkLow ? 1 : 0));
}

void IECBus::peripheralControlData(IECDevice* device, bool dataLow)
{
    if (!device)
        return;

    const RegisteredDevice* registration = findRegisteredDevice(device->getIECDeviceNumber());

    if (!registration || registration->iecDevice != device)
        return;

    const bool old = iecDrivesDataLow[device];

    if (old == dataLow)
        return;

    iecDrivesDataLow[device] = dataLow;

    recalcAndNotify();

    uint64_t cycle = 0;
    uint16_t pc = 0;

    if (Peripheral* peripheral = registration->peripheral)
    {
        if (Drive* drive = peripheral->asDrive())
        {
            if (const CPU* cpu = drive->getDriveCPU())
            {
                cycle = cpu->getTotalCycles();
                pc = cpu->getPC();
            }
        }
    }

    recordTrace(TraceType::PeripheralData, cycle, pc, device->getIECDeviceNumber(), static_cast<uint8_t>(dataLow ? 1 : 0));
}

void IECBus::peripheralControlAtn(IECDevice* device, bool atnLow)
{
    if (!device)
        return;

    const RegisteredDevice* registration = findRegisteredDevice(device->getIECDeviceNumber());

    if (!registration || registration->iecDevice != device)
        return;

    iecDrivesAtnLow[device] = atnLow;

    recalcAndNotify();
}

void IECBus::peripheralControlSrq(IECDevice* device, bool state)
{
    if (!device)
        return;

    const int deviceNumber = device->getIECDeviceNumber();

    // Find the unified registration.
    const RegisteredDevice* registration = findRegisteredDevice(deviceNumber);

    // Verify this exact IECDevice is registered.
    if (!registration || registration->iecDevice != device)
        return;

    // Get the associated legacy Peripheral.
    Peripheral* peripheral = registration->peripheral;

    if (!peripheral)
        return;

    // Preserve existing talker arbitration.
    if (!currentTalker)
        currentTalker = peripheral;

    if (currentTalker != peripheral)
        return;

    // Update IEC-specific SRQ state.
    device->setSRQAsserted(state);

    // Recalculate the resolved SRQ line.
    updateSrqLine();
    updateBusState();
}

bool IECBus::registerDevice(int deviceNumber, Peripheral* peripheral, IECDevice* iecDevice)
{
    if (!peripheral || !iecDevice)
        return false;

    // Reject duplicate IEC addresses.
    if (findRegisteredDevice(deviceNumber))
        return false;

    // Register both interfaces together.
    registeredDevices[deviceNumber] = {peripheral, iecDevice};

    // Preserve the legacy Peripheral identity and connection.
    peripheral->setDeviceNumber(deviceNumber);
    peripheral->attachIECBusInstance(this);

    // Initialize the IECDevice identity and connection.
    iecDevice->iecDeviceNumber = deviceNumber;
    iecDevice->attachIECBusInstance(this);

    // Preserve drive timing bookkeeping.
    driveCycleAccumulators[peripheral] = 0.0;

    // Synchronize the new electrical contribution maps
    // with the IECDevice's current assertion flags.
    iecDrivesClkLow[iecDevice]  = iecDevice->assertClk;
    iecDrivesDataLow[iecDevice] = iecDevice->assertData;
    iecDrivesAtnLow[iecDevice]  = iecDevice->assertAtn;

    // Resolve the electrical state before notifying the device.
    recalcAndNotify();

    // Initialize IECDevice callbacks with the resolved levels.
    iecDevice->atnChanged(!busLines.atn);
    iecDevice->clkChanged(!busLines.clk);
    iecDevice->dataChanged(!busLines.data);

    return true;
}

void IECBus::unregisterDevice(int deviceNumber)
{
    auto it = registeredDevices.find(deviceNumber);

    if (it == registeredDevices.end())
        return;

    Peripheral* peripheral = it->second.peripheral;
    IECDevice* iecDevice = it->second.iecDevice;

    // Remove the registration first.
    registeredDevices.erase(it);

    // Clean up IECDevice.
    if (iecDevice)
    {
        // Remove all electrical contributions from this device.
        iecDrivesClkLow.erase(iecDevice);
        iecDrivesDataLow.erase(iecDevice);
        iecDrivesAtnLow.erase(iecDevice);

        // Reset the device's local electrical assertion state.
        iecDevice->assertClk = false;
        iecDevice->assertData = false;
        iecDevice->assertAtn = false;

        // Clear its connection to the IEC bus.
        iecDevice->detachIECBusInstance();
        iecDevice->iecDeviceNumber = -1;
    }

    // Clean up legacy Peripheral state.
    if (peripheral)
    {
         // Clear the legacy connection without unregistering again.
        peripheral->clearIECBusInstance(this);

        driveCycleAccumulators.erase(peripheral);

        if (currentTalker == peripheral)
            currentTalker = nullptr;

        currentListeners.erase(std::remove(currentListeners.begin(), currentListeners.end(), peripheral), currentListeners.end());
    }

    recalcAndNotify();
}

void IECBus::listen(int deviceNumber)
{
    if (romControlledIEC)
        return;

    const RegisteredDevice* registration = findRegisteredDevice(deviceNumber);

    if (!registration || !registration->iecDevice || !registration->peripheral)
        return;

    Peripheral* peripheral = registration->peripheral;
    IECDevice* device = registration->iecDevice;

    // Preserve existing listener tracking.
    if (std::find(currentListeners.begin(), currentListeners.end(), peripheral) == currentListeners.end())
        currentListeners.push_back(peripheral);

    currentState = State::LISTEN;

    device->onListen();
}

void IECBus::unListen(int deviceNumber)
{
    if (romControlledIEC)
        return;

    // UNLISTEN is global ($3F).
    currentState = State::UNLISTEN;

    for (const auto& [num, registration] : registeredDevices)
    {
        if (IECDevice* device = registration.iecDevice)
            device->onUnListen();
    }

    currentListeners.clear();
}

void IECBus::talk(int deviceNumber)
{
    if (romControlledIEC)
        return;

    const RegisteredDevice* registration = findRegisteredDevice(deviceNumber);

    if (!registration || !registration->iecDevice || !registration->peripheral)
        return;

    // Preserve existing talker tracking.
    currentTalker = registration->peripheral;
    currentState = State::TALK;

    peripheralDrivesClkLow = false;
    peripheralDrivesDataLow = false;

    recalcAndNotify();

    registration->iecDevice->onTalk();
}

void IECBus::unTalk(int deviceNumber)
{
    if (romControlledIEC)
        return;

    // UNTALK is global ($5F).
    currentState = State::UNTALK;

    for (const auto& [num, registration] : registeredDevices)
    {
        if (IECDevice* device = registration.iecDevice)
            device->onUnTalk();
    }

    currentTalker = nullptr;

    peripheralDrivesClkLow = false;
    peripheralDrivesDataLow = false;

    recalcAndNotify();
}

void IECBus::tick(uint64_t cyclesPassed)
{
    for (uint64_t c = 0; c < cyclesPassed; ++c)
    {
        updateSrqLine();
        recalcAndNotify();

        for (const auto& [num, registration] : registeredDevices)
        {
            Peripheral* peripheral = registration.peripheral;

            if (!peripheral)
                continue;

            Drive* drive = peripheral->asDrive();

            if (!drive)
                continue;

            // Preserve existing drive timing behavior.
            drive->tick(1);

            updateSrqLine();
            recalcAndNotify();
        }
    }

    updateSrqLine();
    recalcAndNotify();
}

void IECBus::setHostCpuHz(double hz)
{
    if (hz > 0.0)
        hostCpuHz = hz;
}

IECDevice* IECBus::getIECDevice(int deviceNumber) const
{
    const RegisteredDevice* registration = findRegisteredDevice(deviceNumber);

    if (!registration)
        return nullptr;

    return registration->iecDevice;
}

std::map<int, Peripheral*> IECBus::getRegisteredPeripherals() const
{
    std::map<int, Peripheral*> result;

    for (const auto& [deviceNumber, registration] : registeredDevices)
    {
        if (registration.peripheral)
            result.emplace(deviceNumber, registration.peripheral);
    }

    return result;
}

IECDevice* IECBus::findIECDevice(int deviceNumber) const
{
    const RegisteredDevice* registration =
        findRegisteredDevice(deviceNumber);

    if (!registration)
        return nullptr;

    return registration->iecDevice;
}

Peripheral* IECBus::findRegisteredPeripheral(const IECDevice* device) const
{
    if (!device)
        return nullptr;

    const RegisteredDevice* registration =
        findRegisteredDevice(device->getIECDeviceNumber());

    if (!registration)
        return nullptr;

    if (registration->iecDevice != device)
        return nullptr;

    return registration->peripheral;
}

IECBus::RegisteredDevice* IECBus::findRegisteredDevice(int deviceNumber)
{
    auto it = registeredDevices.find(deviceNumber);

    if (it == registeredDevices.end())
        return nullptr;

    return &it->second;
}

int IECBus::findDeviceNumber(const Peripheral* peripheral) const
{
    if (!peripheral)
        return -1;

    for (const auto& [num, registration] : registeredDevices)
    {
        if (registration.peripheral == peripheral)
            return num;
    }

    return -1;
}

const IECBus::RegisteredDevice* IECBus::findRegisteredDevice(int deviceNumber) const
{
    auto it = registeredDevices.find(deviceNumber);

    if (it == registeredDevices.end())
        return nullptr;

    return &it->second;
}

void IECBus::updateBusState()
{
    bool anyClkLow  = false;
    bool anyDataLow = false;
    bool anyAtnLow  = false;

    // IECDevice is now the authoritative source
    // of peripheral electrical contributions.
    for (const auto& [device, low] : iecDrivesClkLow)
        anyClkLow |= low;

    for (const auto& [device, low] : iecDrivesDataLow)
        anyDataLow |= low;

    for (const auto& [device, low] : iecDrivesAtnLow)
        anyAtnLow |= low;

    // Aggregate peripheral pull-low intentions.
    peripheralDrivesClkLow  = anyClkLow;
    peripheralDrivesDataLow = anyDataLow;
    peripheralDrivesAtnLow  = anyAtnLow;

    // Resolve physical IEC bus levels.
    busLines.updateLineState(c64DrivesClkLow, c64DrivesDataLow, peripheralDrivesClkLow, peripheralDrivesDataLow, c64DrivesAtnLow,
        peripheralDrivesAtnLow);
}

IECBus::DeviceElectricalSnapshot IECBus::snapshotDeviceElectrical(int deviceNumber) const
{
    DeviceElectricalSnapshot s{};

    const RegisteredDevice* registration = findRegisteredDevice(deviceNumber);

    if (!registration)
        return s;

    s.registered = true;

    if (IECDevice* device = registration->iecDevice)
    {
        s.hasIECDevice = true;

        const auto clk = iecDrivesClkLow.find(device);
        const auto data = iecDrivesDataLow.find(device);
        const auto atn = iecDrivesAtnLow.find(device);

        s.iecClkLow = clk != iecDrivesClkLow.end() && clk->second;
        s.iecDataLow = data != iecDrivesDataLow.end() && data->second;
        s.iecAtnLow = atn != iecDrivesAtnLow.end() && atn->second;
    }

    return s;
}

IECBus::PhysicalSnapshot IECBus::snapshotPhysical() const
{
    PhysicalSnapshot s{};

    // Resolved bus wires.
    s.atnHigh  = busLines.atn;
    s.clkHigh  = busLines.clk;
    s.dataHigh = busLines.data;
    s.srqHigh  = line_srqin;

    // C64 pull-low contributions.
    s.c64AtnLow  = c64DrivesAtnLow;
    s.c64ClkLow  = c64DrivesClkLow;
    s.c64DataLow = c64DrivesDataLow;

    // Peripheral pull-low contributions.
    s.peripheralAtnLow  = peripheralDrivesAtnLow;
    s.peripheralClkLow  = peripheralDrivesClkLow;
    s.peripheralDataLow = peripheralDrivesDataLow;

    // IECDevice contributions.
    for (const auto& [dev, low] : iecDrivesAtnLow)
    {
        if (dev && low)
            ++s.peripheralAtnPullers;
    }

    for (const auto& [dev, low] : iecDrivesClkLow)
    {
        if (dev && low)
            ++s.peripheralClkPullers;
    }

    for (const auto& [dev, low] : iecDrivesDataLow)
    {
        if (dev && low)
            ++s.peripheralDataPullers;
    }

    // Existing software protocol state.
    s.legacyState = currentState;
    s.legacyTalkerDevice = currentTalker ? findDeviceNumber(currentTalker) : -1;
    s.legacyListenerCount = static_cast<int>(currentListeners.size());

    return s;
}

void IECBus::updateSrqLine()
{
    bool srqAsserted = false;

    for (const auto& [num, registration] : registeredDevices)
    {
        if (IECDevice* iecDevice = registration.iecDevice)
        {
            if (iecDevice->isSRQAsserted())
            {
                srqAsserted = true;
                break;
            }
        }
        else if (Peripheral* peripheral = registration.peripheral)
        {
            // Legacy Peripheral implementation
            if (peripheral->isSRQAsserted())
            {
                srqAsserted = true;
                break;
            }
        }
    }

    line_srqin = !srqAsserted;

    // Preserve existing CIA2 notification behavior.
    if (cia2)
        cia2->srqChanged(line_srqin);
}

void IECBus::secondaryAddress(uint8_t devNum, uint8_t sa)
{
    if (romControlledIEC)
        return;

    const RegisteredDevice* registration = findRegisteredDevice(devNum);

    if (!registration || !registration->iecDevice)
        return;

    registration->iecDevice->onSecondaryAddress(sa);
}

Peripheral* IECBus::getDevice(int id) const
{
    const RegisteredDevice* registration = findRegisteredDevice(id);

    if (!registration)
        return nullptr;

    return registration->peripheral;
}

void IECBus::debugDumpDevices(const char* tag)
{
    std::cout << "[IECBus] " << tag
              << "  ATN=" << (busLines.atn ? "H" : "L")
              << " CLK=" << (busLines.clk ? "H" : "L")
              << " DATA=" << (busLines.data ? "H" : "L")
              << " state=" << (currentState == State::ATTENTION ? "ATTENTION" : "IDLE")
              << "\n";

    for (const auto& [num, registration] : registeredDevices)
    {
        Peripheral* peripheral = registration.peripheral;

        if (!peripheral)
            continue;

        // Verify electrical contributions for devices using IECDevice.
        if (IECDevice* iecDevice = registration.iecDevice)
        {
            const auto iecClk = iecDrivesClkLow.find(iecDevice);
            const auto iecData = iecDrivesDataLow.find(iecDevice);
            const auto iecAtn = iecDrivesAtnLow.find(iecDevice);

            const bool iecClkLow = iecClk != iecDrivesClkLow.end() && iecClk->second;
            const bool iecDataLow = iecData != iecDrivesDataLow.end() && iecData->second;
            const bool iecAtnLow = iecAtn != iecDrivesAtnLow.end() && iecAtn->second;

            std::cout
                << "  dev " << num
                << " | IECDevice CLK=" << iecClkLow
                << " DATA=" << iecDataLow
                << " ATN=" << iecAtnLow
                << '\n';
        }

        Drive* drive = peripheral->asDrive();

        if (!drive)
        {
            std::cout << "  dev " << num
                      << " (non-drive peripheral)\n";
            continue;
        }

        const auto* diagnostics = dynamic_cast<const IECDriveDiagnostics*>(drive);

        if (!diagnostics)
        {
            std::cout << "  dev " << num
                      << " (IEC diagnostics unavailable)\n";
            continue;
        }

        const IECDriveSnapshot s = diagnostics->snapshotIEC();

        std::cout << "  dev " << num
                  << " atnLow=" << s.atnLow
                  << " clkLow=" << s.clkLow
                  << " dataLow=" << s.dataLow
                  << " drvAssertClk=" << s.drvAssertClk
                  << " drvAssertData=" << s.drvAssertData
                  << " listening=" << s.listening
                  << " talking=" << s.talking
                  << " busState=" << int(s.busState)
                  << "\n";
    }
}

Drive* IECBus::getDriveWithBreakpointHit() const
{
    for (const auto& [num, registration] : registeredDevices)
    {
        Peripheral* peripheral = registration.peripheral;

        if (!peripheral)
            continue;

        Drive* drive = peripheral->asDrive();

        if (drive && drive->isBreakpointHit())
            return drive;
    }

    return nullptr;
}

Drive* IECBus::getDriveWithWatchpointHit()
{
    for (const auto& [num, registration] : registeredDevices)
    {
        Peripheral* peripheral = registration.peripheral;

        if (!peripheral)
            continue;

        Drive* drive = peripheral->asDrive();

        if (drive && drive->isWatchpointHit())
            return drive;
    }

    return nullptr;
}

void IECBus::recalcAndNotify()
{
    const bool oldAtn  = busLines.atn;
    const bool oldClk  = busLines.clk;
    const bool oldData = busLines.data;

    updateBusState();

    // ATN first
    if (busLines.atn != oldAtn)
    {
        const bool atnLow = !busLines.atn;

        if (cia2)
            cia2->atnChanged(atnLow);

        for (const auto& [num, registration] : registeredDevices)
        {
            if (IECDevice* device = registration.iecDevice)
                device->atnChanged(atnLow);
        }
    }

    // CLK
    if (busLines.clk != oldClk)
    {
        const bool clkLow = !busLines.clk;

        if (cia2)
            cia2->clkChanged(clkLow);

        for (const auto& [num, registration] : registeredDevices)
        {
            if (IECDevice* device = registration.iecDevice)
                device->clkChanged(clkLow);
        }
    }

    // DATA
    if (busLines.data != oldData)
    {
        const bool dataLow = !busLines.data;

        if (cia2)
            cia2->dataChanged(dataLow);

        for (const auto& [num, registration] : registeredDevices)
        {
            if (IECDevice* device = registration.iecDevice)
                device->dataChanged(dataLow);
        }
    }
}

std::string IECBus::debugPhysicalSnapshotString() const
{
    const auto s = snapshotPhysical();

    std::ostringstream out;

    out << "IEC physical bus:\n";
    out << "  Resolved lines: "
        << "ATN="  << (s.atnHigh  ? "H" : "L") << "  "
        << "CLK="  << (s.clkHigh  ? "H" : "L") << "  "
        << "DATA=" << (s.dataHigh ? "H" : "L") << "  "
        << "SRQ="  << (s.srqHigh  ? "H" : "L") << "\n";

    out << "  C64 drives low: "
        << "ATN="  << (s.c64AtnLow  ? "yes" : "no") << "  "
        << "CLK="  << (s.c64ClkLow  ? "yes" : "no") << "  "
        << "DATA=" << (s.c64DataLow ? "yes" : "no") << "\n";

    out << "  Peripheral drives low: "
        << "ATN="  << (s.peripheralAtnLow  ? "yes" : "no")
        << " (" << s.peripheralAtnPullers << ")  "
        << "CLK="  << (s.peripheralClkLow  ? "yes" : "no")
        << " (" << s.peripheralClkPullers << ")  "
        << "DATA=" << (s.peripheralDataLow ? "yes" : "no")
        << " (" << s.peripheralDataPullers << ")\n";

    out << "\n";
    out << "Legacy/software IEC decode:\n";
    out << "  State enum: " << static_cast<int>(s.legacyState) << "\n";
    out << "  Talker device: "
        << (s.legacyTalkerDevice >= 0 ? std::to_string(s.legacyTalkerDevice) : "(none)")
        << "\n";
    out << "  Listener count: " << s.legacyListenerCount << "\n";

    return out.str();
}

void IECBus::recordTrace(TraceType type, uint64_t cpuCycle, uint16_t pc, int deviceNumber, uint8_t value)
{
    TraceEntry& e = traceHistory[traceWriteIndex];

    e.valid = true;
    e.type = type;
    e.cpuCycle = cpuCycle;
    e.pc = pc;
    e.deviceNumber = deviceNumber;

    e.atnHigh = busLines.atn;
    e.clkHigh = busLines.clk;
    e.dataHigh = busLines.data;

    e.value = value;

    traceWriteIndex = (traceWriteIndex + 1) % traceHistory.size();

    if (traceCount < traceHistory.size())
        ++traceCount;
}

void IECBus::recordC64Read(uint64_t cpuCycle, uint16_t pc, uint8_t value)
{
    // Do not let an endless C64 polling loop overwrite the
    // useful IEC transitions that occurred immediately before it.
    if (traceCount > 0)
    {
        const size_t capacity = traceHistory.size();
        const size_t lastIndex = (traceWriteIndex + capacity - 1) % capacity;

        const TraceEntry& last = traceHistory[lastIndex];

        if (last.valid &&
            last.type == TraceType::C64Read &&
            last.value == value &&
            last.atnHigh == busLines.atn &&
            last.clkHigh == busLines.clk &&
            last.dataHigh == busLines.data)
        {
            return;
        }
    }

    recordTrace(TraceType::C64Read, cpuCycle, pc, -1, value);
}

void IECBus::recordC64Output(uint64_t cpuCycle, uint16_t pc, uint8_t value)
{
    if (traceCount > 0)
    {
        const size_t capacity = traceHistory.size();
        const size_t lastIndex = (traceWriteIndex + capacity - 1) % capacity;
        const TraceEntry& last = traceHistory[lastIndex];

        if (last.valid &&
            last.type == TraceType::C64Output &&
            last.value == value &&
            last.atnHigh == busLines.atn &&
            last.clkHigh == busLines.clk &&
            last.dataHigh == busLines.data)
        {
            return;
        }
    }

    recordTrace(TraceType::C64Output, cpuCycle, pc, -1, value);
}

void IECBus::clearTraceHistory()
{
    for (auto& e : traceHistory)
        e = TraceEntry{};

    traceWriteIndex = 0;
    traceCount = 0;
}
