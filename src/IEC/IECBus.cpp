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
    wrtr.writeU32(1); //version

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

    // Serialize all registered IEC device contributions.
    wrtr.writeU8(static_cast<uint8_t>(registeredDevices.size()));

    for (const auto& [num, registration] : registeredDevices)
    {
        Peripheral* peripheral = registration.peripheral;

        wrtr.writeI32(static_cast<int32_t>(num));

        // Preserve legacy electrical contribution values.
        const bool clkLow = peripheral && devDrivesClkLow.count(peripheral) ? devDrivesClkLow.at(peripheral) : false;
        const bool dataLow = peripheral && devDrivesDataLow.count(peripheral) ? devDrivesDataLow.at(peripheral) : false;
        const bool atnLow = peripheral && devDrivesAtnLow.count(peripheral) ? devDrivesAtnLow.at(peripheral) : false;

        wrtr.writeBool(clkLow);
        wrtr.writeBool(dataLow);
        wrtr.writeBool(atnLow);
    }

    wrtr.writeBool(romControlledIEC);

    wrtr.endChunk();
}

bool IECBus::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "IEC0", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    uint32_t ver = 0;
    if (!rdr.readU32(ver))                                          { rdr.exitChunkPayload(chunk); return false; }
    if (ver != 1)                                                   { rdr.exitChunkPayload(chunk); return false; }

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

    // Clear current contribution maps (IMPORTANT)
    devDrivesClkLow.clear();
    devDrivesDataLow.clear();
    devDrivesAtnLow.clear();

    for (uint8_t i = 0; i < devCount; ++i)
    {
        int32_t id = -1;
        bool clkLow=false, dataLow=false, atnLow=false;

        if (!rdr.readI32(id))                                       { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(clkLow))                                  { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(dataLow))                                 { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(atnLow))                                  { rdr.exitChunkPayload(chunk); return false; }

        const RegisteredDevice* registration =
        findRegisteredDevice(static_cast<int>(id));

        if (registration && registration->peripheral)
        {
            Peripheral* peripheral = registration->peripheral;

            devDrivesClkLow[peripheral]  = clkLow;
            devDrivesDataLow[peripheral] = dataLow;
            devDrivesAtnLow[peripheral]  = atnLow;
        }
    }

    if (!rdr.readBool(romControlledIEC))                            { rdr.exitChunkPayload(chunk); return false; }

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

    // Re-resolve and notify everyone
    updateSrqLine();
    recalcAndNotify();

    rdr.exitChunkPayload(chunk);

    return true;
}

void IECBus::reset()
{
    // Reset bus-level state machine
    currentState   = State::IDLE;
    currentTalker  = nullptr;
    currentListeners.clear();

    // Release C64-driven open-collector lines (idle = HIGH)
    c64DrivesAtnLow  = false;
    c64DrivesClkLow  = false;
    c64DrivesDataLow = false;

    // Release SRQ (idle = HIGH)
    line_srqin = true;

    // Clear *all* device pull-low contributions so nothing "sticks low"
    devDrivesClkLow.clear();
    devDrivesDataLow.clear();
    devDrivesAtnLow.clear();

    // Recompute resolved line levels (should become HIGH/HIGH/HIGH)
    updateBusState();

    // Sync CIA2 + devices to the resolved state so they have correct baseline.
    // We do NOT want to rely on edge detection during reset.
    const bool atnLow  = !busLines.atn;
    const bool clkLow  = !busLines.clk;
    const bool dataLow = !busLines.data;

    // Compatibility mode
    romControlledIEC = false;

    if (cia2)
    {
        cia2->atnChanged(atnLow);
        cia2->clkChanged(clkLow);
        cia2->dataChanged(dataLow);
        cia2->srqChanged(line_srqin);
    }

    for (const auto& [num, registration] : registeredDevices)
    {
        if (IECDevice* iecDevice = registration.iecDevice)
        {
            // New IECDevice interface
            iecDevice->atnChanged(atnLow);
            iecDevice->clkChanged(clkLow);
            iecDevice->dataChanged(dataLow);
        }
        else if (Peripheral* peripheral = registration.peripheral)
        {
            // Legacy Peripheral interface
            peripheral->atnChanged(atnLow);
            peripheral->clkChanged(clkLow);
            peripheral->dataChanged(dataLow);
        }
    }

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

void IECBus::peripheralControlClk(Peripheral* device, bool clkLow)
{
    const int deviceNumber = findDeviceNumber(device);

    if (deviceNumber < 0)
        return;

    const bool old = devDrivesClkLow[device];

    devDrivesClkLow[device] = clkLow;

    recalcAndNotify();

    if (old != clkLow)
    {
        uint64_t cycle = 0;
        uint16_t pc = 0;

        if (Drive* drive = device->asDrive())
        {
            if (const CPU* cpu = drive->getDriveCPU())
            {
                cycle = cpu->getTotalCycles();
                pc = cpu->getPC();
            }
        }

        recordTrace(TraceType::PeripheralClk, cycle, pc, deviceNumber, static_cast<uint8_t>(clkLow ? 1 : 0));
    }
}

void IECBus::peripheralControlData(Peripheral* device, bool dataLow)
{
    const int deviceNumber = findDeviceNumber(device);

    if (deviceNumber < 0)
        return;

    const bool old = devDrivesDataLow[device];

    devDrivesDataLow[device] = dataLow;

    recalcAndNotify();

    if (old != dataLow)
    {
        uint64_t cycle = 0;
        uint16_t pc = 0;

        if (Drive* drive = device->asDrive())
        {
            if (const CPU* cpu = drive->getDriveCPU())
            {
                cycle = cpu->getTotalCycles();
                pc = cpu->getPC();
            }
        }

        recordTrace(TraceType::PeripheralData, cycle, pc, deviceNumber, static_cast<uint8_t>(dataLow ? 1 : 0)
        );
    }
}

void IECBus::peripheralControlAtn(Peripheral* device, bool atnLow)
{
    if (findDeviceNumber(device) < 0)
        return;

    devDrivesAtnLow[device] = atnLow;

    recalcAndNotify();
}

void IECBus::peripheralControlSrq(Peripheral* device, bool state)
{
    if (findDeviceNumber(device) < 0)
        return;

    // Preserve existing talker arbitration.
    if (currentTalker == nullptr)
        currentTalker = device;

    if (currentTalker != device)
        return;

    device->setSRQAsserted(state);

    updateSrqLine();
    updateBusState();

}
void IECBus::peripheralControlClk(IECDevice* device, bool state)
{
    Peripheral* peripheral = findRegisteredPeripheral(device);

    if (!peripheral)
        return;

    peripheralControlClk(peripheral, state);
}

void IECBus::peripheralControlData(IECDevice* device, bool state)
{
    Peripheral* peripheral = findRegisteredPeripheral(device);

    if (!peripheral)
        return;

    peripheralControlData(peripheral, state);
}

void IECBus::peripheralControlAtn(IECDevice* device, bool state)
{
    Peripheral* peripheral = findRegisteredPeripheral(device);

    if (!peripheral)
        return;

    peripheralControlAtn(peripheral, state);
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

void IECBus::registerDevice(int deviceNumber, Peripheral* device)
{
    if (!device)
        return;

    if (findRegisteredDevice(deviceNumber))
        return;

    device->setDeviceNumber(deviceNumber);

    // Authoritative registration.
    registeredDevices[deviceNumber] = { device, nullptr };

    // Temporary compatibility map.
    devices[deviceNumber] = device;

    device->attachIECBusInstance(this);
    driveCycleAccumulators[device] = 0.0;

    recalcAndNotify();

    device->atnChanged(!busLines.atn);
    device->clkChanged(!busLines.clk);
    device->dataChanged(!busLines.data);
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

    // Temporary compatibility map.
    devices.erase(deviceNumber);

    // Clean up IECDevice.
    if (iecDevice)
    {
        iecDevice->detachIECBusInstance();
        iecDevice->iecDeviceNumber = -1;
    }

    // Clean up legacy Peripheral state.
    if (peripheral)
    {
         // Clear the legacy connection without unregistering again.
        peripheral->clearIECBusInstance(this);

        devDrivesClkLow.erase(peripheral);
        devDrivesDataLow.erase(peripheral);
        devDrivesAtnLow.erase(peripheral);

        driveCycleAccumulators.erase(peripheral);

        if (currentTalker == peripheral)
            currentTalker = nullptr;

        currentListeners.erase(std::remove(currentListeners.begin(), currentListeners.end(), peripheral), currentListeners.end());
    }

    recalcAndNotify();
}

bool IECBus::registerDevice(int deviceNumber, Peripheral* peripheral, IECDevice* iecDevice)
{
    if (!peripheral || !iecDevice)
        return false;

    if (findRegisteredDevice(deviceNumber))
        return false;

    // Register using the existing legacy path.
    registerDevice(deviceNumber, peripheral);

    RegisteredDevice* registration = findRegisteredDevice(deviceNumber);

    if (!registration || registration->peripheral != peripheral)
        return false;

    // Attach the IEC-specific interface.
    registration->iecDevice = iecDevice;

    iecDevice->iecDeviceNumber = deviceNumber;
    iecDevice->attachIECBusInstance(this);

    // Do not dispatch initial callbacks a second time.
    return true;
}

void IECBus::listen(int deviceNumber)
{
    if (romControlledIEC)
        return;

    const RegisteredDevice* registration = findRegisteredDevice(deviceNumber);

    if (!registration)
        return;

    Peripheral* peripheral = registration->peripheral;
    IECDevice* iecDevice = registration->iecDevice;

    if (!peripheral)
        return;

    // Preserve existing listener tracking.
    if (std::find(currentListeners.begin(), currentListeners.end(), peripheral) == currentListeners.end())
        currentListeners.push_back(peripheral);

    currentState = State::LISTEN;

    if (iecDevice)
        iecDevice->onListen();
    else
        peripheral->onListen();
}

void IECBus::unListen(int deviceNumber)
{
    if (romControlledIEC)
        return;

    // UNLISTEN is global ($3F).
    currentState = State::UNLISTEN;

    for (const auto& [num, registration] : registeredDevices)
    {
        if (registration.iecDevice)
            registration.iecDevice->onUnListen();
        else if (registration.peripheral)
            registration.peripheral->onUnListen();
    }

    currentListeners.clear();
}

void IECBus::talk(int deviceNumber)
{
    if (romControlledIEC)
        return;

    const RegisteredDevice* registration = findRegisteredDevice(deviceNumber);

    if (!registration || !registration->peripheral)
        return;

    currentTalker = registration->peripheral;
    currentState = State::TALK;

    peripheralDrivesClkLow = false;
    peripheralDrivesDataLow = false;

    recalcAndNotify();

    if (registration->iecDevice)
        registration->iecDevice->onTalk();
    else
        registration->peripheral->onTalk();
}

void IECBus::unTalk(int deviceNumber)
{
    if (romControlledIEC)
        return;

    // UNTALK is global ($5F).
    currentState = State::UNTALK;

    for (const auto& [num, registration] : registeredDevices)
    {
        if (registration.iecDevice)
            registration.iecDevice->onUnTalk();
        else if (registration.peripheral)
            registration.peripheral->onUnTalk();
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

    for (auto& [dev, v] : devDrivesClkLow)  anyClkLow  |= v;
    for (auto& [dev, v] : devDrivesDataLow) anyDataLow |= v;
    for (auto& [dev, v] : devDrivesAtnLow)  anyAtnLow  |= v;

    peripheralDrivesClkLow  = anyClkLow;
    peripheralDrivesDataLow = anyDataLow;
    peripheralDrivesAtnLow  = anyAtnLow;

    busLines.updateLineState(
        c64DrivesClkLow, c64DrivesDataLow,
        peripheralDrivesClkLow, peripheralDrivesDataLow,
        c64DrivesAtnLow, peripheralDrivesAtnLow
    );
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

    for (const auto& [dev, low] : devDrivesAtnLow)
    {
        if (dev && low)
            ++s.peripheralAtnPullers;
    }

    for (const auto& [dev, low] : devDrivesClkLow)
    {
        if (dev && low)
            ++s.peripheralClkPullers;
    }

    for (const auto& [dev, low] : devDrivesDataLow)
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

    if (!registration)
        return;

    if (registration->iecDevice)
        registration->iecDevice->onSecondaryAddress(sa);
    else if (registration->peripheral)
        registration->peripheral->onSecondaryAddress(sa);
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

        Drive* drive = peripheral->asDrive();

        if (!drive)
        {
            std::cout << "  dev " << num
                      << " (non-drive peripheral)\n";
            continue;
        }

        auto s = drive->snapshotIEC();

        std::cout << "  dev " << num
                  << " atnLow=" << s.atnLow
                  << " clkLow=" << s.clkLow
                  << " dataLow=" << s.dataLow
                  << " drvAssertClk=" << s.drvAssertClk
                  << " drvAssertData=" << s.drvAssertData
                  << " listening=" << s.listening
                  << " talking=" << s.talking
                  << " busState=" << int(s.busState)
                  << " waitingForAck=" << s.waitingForAck
                  << " talkQueueLen=" << s.talkQueueLen
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
            if (IECDevice* iecDevice = registration.iecDevice)
                iecDevice->atnChanged(atnLow);
            else if (Peripheral* peripheral = registration.peripheral)
                peripheral->atnChanged(atnLow);
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
            if (IECDevice* iecDevice = registration.iecDevice)
                iecDevice->clkChanged(clkLow);
            else if (Peripheral* peripheral = registration.peripheral)
                peripheral->clkChanged(clkLow);
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
            if (IECDevice* iecDevice = registration.iecDevice)
                iecDevice->dataChanged(dataLow);
            else if (Peripheral* peripheral = registration.peripheral)
                peripheral->dataChanged(dataLow);
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
