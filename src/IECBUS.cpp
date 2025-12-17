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
    updateBusState(); // Update bus with defaults
}

IECBUS::~IECBUS() = default;

void IECBUS::setAtnLine(bool state)
{
    const bool oldAtn  = busLines.atn;
    const bool oldClk  = busLines.clk;
    const bool oldData = busLines.data;

    c64DrivesAtnLow = !state;
    currentState = (!state) ? State::ATTENTION : State::IDLE;

    updateBusState();

    // If ATN level actually changed, notify ATN FIRST
    if (busLines.atn != oldAtn)
    {
        const bool atnLow = !busLines.atn;

        if (cia2object)
            cia2object->atnChanged(atnLow);

        for (auto const& [num, dev] : devices)
            if (dev) dev->atnChanged(atnLow);
    }

    // Only generate CLK/DATA notifications if those lines truly changed electrically
    if (busLines.clk != oldClk)
    {
        if (cia2object)
            cia2object->clkChanged(busLines.clk);

        for (auto const& [num, dev] : devices)
            if (dev) dev->clkChanged(!busLines.clk);
    }

    if (busLines.data != oldData)
    {
        if (cia2object)
            cia2object->dataChanged(busLines.data);

        for (auto const& [num, dev] : devices)
            if (dev) dev->dataChanged(!busLines.data);
    }
}

void IECBUS::setClkLine(bool state)
{
    // Remember the old resolved CLK level *before* the write.
    bool oldClk = busLines.clk;

    // C64 drives CLK low when state == false.
    c64DrivesClkLow = !state;
    updateBusState();

    // If the resolved bus CLK level didn't change, there is no real edge.
    if (busLines.clk == oldClk)
        return;

    #ifdef Debug
    if (!busLines.atn)  // only log while ATN is low (handshake phase)
    {
        std::cout << "[IECBUS] CLK edge from C64: busClk="
                  << (busLines.clk ? "H" : "L")
                  << " (C64 wrote state=" << state << ")\n";
        debugDumpDevices("after C64 CLK edge");
    }
    #endif

    // Now we have a real electrical change - notify CIA2 and drives.
    if (cia2object)
        cia2object->clkChanged(busLines.clk);

    for (auto& [num, dev] : devices)
    {
        if (dev)
            dev->clkChanged(!busLines.clk);
    }
}

void IECBUS::setDataLine(bool state)
{
    bool oldData = busLines.data;

    c64DrivesDataLow = !state;
    updateBusState();

    // Only log / notify if the visible DATA level actually changed.
    if (busLines.data == oldData)
        return;

    #ifdef Debug
    if (!busLines.atn)
    {
        std::cout << "[IECBUS] DATA change from C64: busData="
                  << (busLines.data ? "H" : "L")
                  << " (C64 wrote state=" << state << ")\n";
        debugDumpDevices("after C64 DATA change");
    }
    #endif

    if (cia2object)
        cia2object->dataChanged(busLines.data);

    for (auto const& [num, dev] : devices)
    {
        if (dev) dev->dataChanged(!busLines.data);
    }
}

void IECBUS::setSrqLine(bool state)
{
    bool old = line_srqin;
    line_srqin = state;
    updateBusState();
    // Only notify CIA2 if it actually changed
    if (old != line_srqin) cia2object->srqChanged(line_srqin);
}

void IECBUS::peripheralControlClk(Peripheral* device, bool clkLow)
{
    if (!device) return;
    if (devices.find(device->getDeviceNumber()) == devices.end()) return;

    // Remember old bus CLK so we only notify on real changes
    bool oldClk = busLines.clk;

    // Open-collector behaviour:
    //  clkLow == true  -> this peripheral wants the line LOW
    //  clkLow == false -> this peripheral releases its pull-down
    peripheralDrivesClkLow = clkLow;

    // (Optional) do *not* mess with currentTalker here – just leave
    // your currentTalker logic driven by commands / data if you have it.

    updateBusState();

    if (busLines.clk != oldClk)
    {
        #ifdef Debug
        if (!busLines.atn)
        {
            std::cout << "[IECBUS] CLK edge from dev "
                      << device->getDeviceNumber()
                      << " -> busClk=" << (busLines.clk ? "H" : "L")
                      << " (clkLow=" << clkLow << ")\n";
            debugDumpDevices("after dev CLK edge");
        }
        #endif
        if (cia2object)
            cia2object->clkChanged(busLines.clk);   // CIA sees true = high, false = low

        for (auto& [num, dev] : devices)
        {
            if (dev)
                dev->clkChanged(!busLines.clk);     // drives see "low" as true
        }
    }
}

void IECBUS::peripheralControlData(Peripheral* device, bool stateLow)
{
    if (!device) return;
    if (devices.find(device->getDeviceNumber()) == devices.end()) return;

    // stateLow == true  => peripheral pulls DATA low
    // stateLow == false => releases DATA
    bool oldData = busLines.data;

    peripheralDrivesDataLow = stateLow;
    updateBusState();   // recompute busLines.data from C64 + peripherals

    if (busLines.data != oldData)
    {
        #ifdef Debug
        if (!busLines.atn)
        {
            std::cout << "[IECBUS] DATA change from dev "
                      << device->getDeviceNumber()
                      << " -> busData=" << (busLines.data ? "H" : "L")
                      << " (stateLow=" << stateLow << ")\n";
            debugDumpDevices("after dev DATA change");
        }
        #endif

        // CIA2 sees bus level directly (true = high)
        if (cia2object)
            cia2object->dataChanged(busLines.data);

        // Drives get "dataLow" semantics, so invert
        for (auto const& [num, dev] : devices)
        {
            if (dev) dev->dataChanged(!busLines.data);
        }
    }
}

void IECBUS::peripheralControlAtn(Peripheral* device, bool state)
{
    if (device == nullptr) return;

    // Check that the device is registered.
    if (devices.find(device->getDeviceNumber()) == devices.end()) return;

    // If no talker is active, assign this device as the current talker.
    if (currentTalker == nullptr) currentTalker = device;

    // Only allow the current talker to drive the ATN line.
    if (device != currentTalker) return;

    // Record the peripheral’s intent: true = asserting LOW
    peripheralDrivesAtnLow = state;

    // If device released all lines (CLK, DATA, ATN), drop talker
    if (!peripheralDrivesClkLow && !peripheralDrivesDataLow && !peripheralDrivesAtnLow) currentTalker = nullptr;

    // Recompute the overall bus lines
    updateBusState();

    bool atnNowLow = !busLines.atn;
    for (auto const& [num, dev] : devices)
    {
        if (dev) dev->atnChanged(atnNowLow);
    }

    // Update the CIA2
    if (cia2object) cia2object->atnChanged(atnNowLow);
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
    // Exit if no device attached
    if (device == nullptr) return;

    // Check if a device with this number is already registered
    if (devices.find(deviceNumber) != devices.end()) return;

    // Add device to the map
    devices[deviceNumber] = device;

    // Attach the IECBUS pointer to the device
    device->attachBusInstance(this);

    // Finally update bus state for good measure
    updateBusState();
}

void IECBUS::unregisterDevice(int deviceNumber)
{
    // First find the device to remove in the map
    auto it = devices.find(deviceNumber);

    if (it != devices.end())
    {
        Peripheral* device = it->second;

        // If this device is the current talker clear the pointer
        if (currentTalker == device) currentTalker = nullptr;

        // Also remove the device from any currentListeners
        currentListeners.erase(std::remove(currentListeners.begin(), currentListeners.end(), device),
            currentListeners.end());

        // Remove the devices registration
        devices.erase(it);

        // Finally update the bus state
        updateBusState();
    }
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

void IECBUS::unListen(int deviceNumber)
{
    auto it = devices.find(deviceNumber);
    if (it == devices.end()) return;

    Peripheral* dev = it->second;
    currentListeners.erase(std::remove(currentListeners.begin(), currentListeners.end(), dev), currentListeners.end());

    currentState = State::UNLISTEN;
    dev->onUnListen();
}

void IECBUS::talk(int deviceNumber)
{
    auto it = devices.find(deviceNumber);
    if (it == devices.end()) return;

    auto dev = it->second;
    currentTalker = dev;
    currentState = State::TALK;
    // When a device starts talking, it should release DATA/CLK lines by default until it drives them
    peripheralDrivesClkLow = false;
    peripheralDrivesDataLow = false;
    updateBusState();
    currentTalker->onTalk();
}

void IECBUS::unTalk(int deviceNumber)
{
    auto it = devices.find(deviceNumber);
    if (it == devices.end()) return;

    // Only call onUnTalk if there is actually a current talker!
    if (currentTalker)
    {
        currentTalker->onUnTalk();
    }

    currentTalker = nullptr;
    currentState = State::UNTALK;
    // Let the C64 regain control of the lines
    peripheralDrivesClkLow = peripheralDrivesDataLow = false;
    updateBusState();
}

void IECBUS::tick(uint64_t cyclesPassed)
{
    updateSrqLine();

    updateBusState();
}

void IECBUS::updateBusState()
{
    busLines.updateLineState(c64DrivesClkLow, c64DrivesDataLow, peripheralDrivesClkLow, peripheralDrivesDataLow,
        c64DrivesAtnLow, peripheralDrivesAtnLow);
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
