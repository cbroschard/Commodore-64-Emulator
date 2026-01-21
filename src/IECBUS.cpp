// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "CIA2.h"
#include "Drive/Drive.h"
#include "IECBUS.h"

IECBUS::IECBUS() :
    //Initialize to defaults
    currentState(State::IDLE),
    cia2object(nullptr),
    currentTalker(nullptr),
    line_srqin(true),
    c64DrivesAtnLow(false),
    c64DrivesClkLow(false),
    c64DrivesDataLow(false),
    peripheralDrivesClkLow(false),
    peripheralDrivesDataLow(false),
    peripheralDrivesAtnLow(false),
    lastClk(true)
{
    currentListeners.clear(); // Ensure listener list is empty on start
    reset();
}

IECBUS::~IECBUS() = default;

void IECBUS::reset()
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

    // CRITICAL: clear *all* device pull-low contributions so nothing "sticks low"
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

    if (cia2object)
    {
        cia2object->atnChanged(atnLow);
        cia2object->clkChanged(clkLow);
        cia2object->dataChanged(dataLow);
        cia2object->srqChanged(line_srqin);
    }

    for (auto const& [num, dev] : devices)
    {
        if (!dev) continue;
        dev->atnChanged(atnLow);
        dev->clkChanged(clkLow);
        dev->dataChanged(dataLow);
    }

    lastClk = busLines.clk;
}

void IECBUS::setAtnLine(bool state)
{
    c64DrivesAtnLow = !state;
    currentState = c64DrivesAtnLow ? State::ATTENTION : State::IDLE;

    recalcAndNotify();
}

void IECBUS::setClkLine(bool state)
{
    c64DrivesClkLow = !state;
    recalcAndNotify();
}

void IECBUS::setDataLine(bool state)
{
    c64DrivesDataLow = !state;
    recalcAndNotify();
}

void IECBUS::setSrqLine(bool state)
{
    bool old = line_srqin;
    line_srqin = state;
    updateBusState();
    // Only notify CIA2 if it actually changed
    if (old != line_srqin && cia2object) cia2object->srqChanged(line_srqin);
}

void IECBUS::peripheralControlClk(Peripheral* device, bool clkLow)
{
    if (!device) return;
    if (devices.find(device->getDeviceNumber()) == devices.end()) return;

    devDrivesClkLow[device] = clkLow;
    recalcAndNotify();
}

void IECBUS::peripheralControlData(Peripheral* device, bool dataLow)
{
    if (!device) return;
    if (devices.find(device->getDeviceNumber()) == devices.end()) return;

    devDrivesDataLow[device] = dataLow;
    recalcAndNotify();
}

void IECBUS::peripheralControlAtn(Peripheral* device, bool atnLow)
{
    if (!device) return;
    if (devices.find(device->getDeviceNumber()) == devices.end()) return;

    devDrivesAtnLow[device] = atnLow;
    recalcAndNotify();
}

void IECBUS::peripheralControlSrq(Peripheral* device, bool state)
{
    if (device == nullptr) return;

    // Check that the device is registered.
    if (devices.find(device->getDeviceNumber()) == devices.end()) return;

    // If no talker is active, assign this device as the current talker.
    if (currentTalker == nullptr) currentTalker = device;

    // Only allow the current talker to drive the SRQ line.
    if (device != currentTalker) return;

    device->setSRQAsserted(state);

    // After marking the device, rebuild the SRQ line and overall bus state
    updateSrqLine();
    updateBusState();
}

void IECBUS::registerDevice(int deviceNumber, Peripheral* device)
{
    if (!device) return;
    if (devices.find(deviceNumber) != devices.end()) return;

    devices[deviceNumber] = device;
    device->attachBusInstance(this);

    // Bring busLines up to date and notify any changes to existing devices
    recalcAndNotify();

    // Sync the new device to current levels (no edge required)
    device->atnChanged(!busLines.atn);
    device->clkChanged(!busLines.clk);
    device->dataChanged(!busLines.data);
}

void IECBUS::unregisterDevice(int deviceNumber)
{
    auto it = devices.find(deviceNumber);
    if (it == devices.end()) return;

    Peripheral* device = it->second;

    devDrivesClkLow.erase(device);
    devDrivesDataLow.erase(device);
    devDrivesAtnLow.erase(device);

    if (currentTalker == device) currentTalker = nullptr;

    currentListeners.erase(
        std::remove(currentListeners.begin(), currentListeners.end(), device),
        currentListeners.end()
    );

    devices.erase(it);

    recalcAndNotify();
}

void IECBUS::listen(int deviceNumber)
{
    auto it = devices.find(deviceNumber);
    if (it == devices.end()) return;

    Peripheral* dev = it->second;
    // Avoid duplicates
    if (std::find(currentListeners.begin(), currentListeners.end(), dev) == currentListeners.end()) currentListeners.push_back(dev);

    currentState = State::LISTEN;
    dev->onListen();
}

void IECBUS::unListen(int /*deviceNumber*/)
{
    // UNLISTEN is global ($3F)
    currentState = State::UNLISTEN;

    for (auto const& [num, dev] : devices)
        if (dev) dev->onUnListen();

    currentListeners.clear();
}

void IECBUS::talk(int deviceNumber)
{
    auto it = devices.find(deviceNumber);
    if (it == devices.end()) return;

    currentTalker = it->second;
    currentState  = State::TALK;

    peripheralDrivesClkLow  = false;
    peripheralDrivesDataLow = false;

    recalcAndNotify();
    currentTalker->onTalk();
}

void IECBUS::unTalk(int /*deviceNumber*/)
{
    // UNTALK is global ($5F)
    currentState = State::UNTALK;

    for (auto const& [num, dev] : devices)
        if (dev) dev->onUnTalk();

    currentTalker = nullptr;

    peripheralDrivesClkLow  = false;
    peripheralDrivesDataLow = false;

    recalcAndNotify();
}

void IECBUS::tick(uint64_t cyclesPassed)
{
    updateSrqLine();
    recalcAndNotify();
}

void IECBUS::updateBusState()
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

void IECBUS::updateSrqLine()
{
    bool srqAsserted = false;
    for (const auto& pair : devices)
    {
        Peripheral* device = pair.second;
        if (device && device->isSRQAsserted())
        {
            srqAsserted = true;
            break;
        }
    }
    line_srqin = !srqAsserted;
    // Update the CIA2
    if(cia2object) cia2object->srqChanged(line_srqin);
}

void IECBUS::secondaryAddress(uint8_t devNum, uint8_t sa)
{
    auto it = devices.find(devNum);
    if (it == devices.end())
        return;

   it->second->onSecondaryAddress(sa);
}

Peripheral* IECBUS::getDevice(int id) const
{
    auto it = devices.find(static_cast<uint8_t>(id));
    if (it == devices.end())
    {
        return nullptr;
    }
    return it->second;
}

void IECBUS::debugDumpDevices(const char* tag)
{
    std::cout << "[IECBUS] " << tag
              << "  ATN=" << (busLines.atn ? "H" : "L")
              << " CLK=" << (busLines.clk ? "H" : "L")
              << " DATA=" << (busLines.data ? "H" : "L")
              << " state=" << (currentState == State::ATTENTION ? "ATTENTION" : "IDLE")
              << "\n";

    for (auto const& [num, dev] : devices)
    {
        if (!dev) continue;

        auto* drive = dynamic_cast<Drive*>(dev);
        if (!drive)
        {
            std::cout << "  dev " << int(num)
                      << " (non-drive peripheral)\n";
            continue;
        }

        auto s = drive->snapshotIEC();   // already implemented in D1541/D1571

        std::cout << "  dev " << int(num)
                  << " atnLow="   << s.atnLow
                  << " clkLow="   << s.clkLow
                  << " dataLow="  << s.dataLow
                  << " drvAssertClk="  << s.drvAssertClk
                  << " drvAssertData=" << s.drvAssertData
                  << " listening="<< s.listening
                  << " talking="  << s.talking
                  << " busState=" << int(s.busState)
                  << " waitingForAck=" << s.waitingForAck
                  << " talkQueueLen="  << s.talkQueueLen
                  << "\n";
    }
}

void IECBUS::recalcAndNotify()
{
    const bool oldAtn  = busLines.atn;
    const bool oldClk  = busLines.clk;
    const bool oldData = busLines.data;

    updateBusState();

    // ATN first
    if (busLines.atn != oldAtn)
    {
        const bool atnLow = !busLines.atn;

        if (cia2object)
            cia2object->atnChanged(atnLow);

        for (auto const& [num, dev] : devices)
            if (dev) dev->atnChanged(atnLow);
    }

    if (busLines.clk != oldClk)
    {
        const bool clkLow = !busLines.clk;

        if (cia2object)
            cia2object->clkChanged(clkLow);

        for (auto const& [num, dev] : devices)
            if (dev) dev->clkChanged(clkLow);
    }

    if (busLines.data != oldData)
    {
        const bool dataLow = !busLines.data;

        if (cia2object)
            cia2object->dataChanged(dataLow);

        for (auto const& [num, dev] : devices)
            if (dev) dev->dataChanged(dataLow);
    }
}
