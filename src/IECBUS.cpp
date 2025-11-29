// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "CIA2.h"
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

void IECBUS::setAtnLine(bool c64ReleasesLine)
{
    bool oldBusATN = busLines.atn;

    c64DrivesAtnLow = !c64ReleasesLine;

    // Recompute bus lines from all drivers (C64 + peripherals)
    updateBusState();

    bool atnNowLow = !busLines.atn;

    #ifdef Debug
    std::cout << "[BUS] setAtnLine(" << c64ReleasesLine
              << ") oldATN=" << oldBusATN
              << " newBusATN=" << busLines.atn
              << " atnNowLow=" << atnNowLow << "\n";
    #endif

    // Update high-level bus state
    currentState = atnNowLow ? State::ATTENTION : State::IDLE;

    // Notify CIA2: it wants "ATN is LOW?" as the argument.
    if (cia2object)
        cia2object->atnChanged(atnNowLow);

    // Notify all peripherals (drives, etc.), which also want "ATN is LOW?"
    for (auto& [num, dev] : devices)
    {
        if (dev)
            dev->atnChanged(atnNowLow);
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
    std::cout << "[IECBUS] setClkLine from C64: state=" << state
              << " oldBusClk=" << oldClk
              << " newBusClk=" << busLines.clk
              << " ATN=" << (busLines.atn ? "H" : "L")
              << "\n";
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
    std::cout << "[BUS] setDataLine(" << state
              << ") -> bus DATA=" << int(busLines.data) << "\n";
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

    #ifdef Debug
    std::cout << "[IECBUS] registerDevice " << deviceNumber
          << " at ptr=" << device << "\n";
    #endif
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
    #ifdef Debug
    std::cout << "[IECBUS] listen(" << deviceNumber << ")\n";
    #endif

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

    currentTalker->onUnTalk();
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

    #ifdef Debug
    std::cout << "[BUS] state=" << static_cast<int>(currentState)
              << " C64ATNlow=" << c64DrivesAtnLow
              << " PeriphATNlow=" << peripheralDrivesAtnLow
              << " busATN=" << busLines.atn
              << "\n";
    #endif
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
