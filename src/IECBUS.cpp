// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "IECBUS.h"

IECBUS::IECBUS() :
    //Initialize to defaults
    line_srqin(true), // SRQ starts high (inactive)
    c64DrivesAtnLow(false),
    c64DrivesClkLow(false),
    c64DrivesDataLow(false),
    peripheralDrivesClkLow(false),
    peripheralDrivesDataLow(false),
    peripheralDrivesAtnLow(false),
    currentState(State::IDLE)
{
    currentListeners.clear(); // Ensure listener list is empty on start
    updateBusState(); // Update bus with defaults
}

IECBUS::~IECBUS() = default;

void IECBUS::setAtnLine(bool state)
{
    // Capture old state first
    bool oldATNState = busLines.atn;

    this->c64DrivesAtnLow = !state;
    updateBusState();

    // Now check if the state actually changed
    if (oldATNState != busLines.atn)
    {
        bool atnNowLow = !busLines.atn; // true if ATN is low

        // Notify all peripherals of the change
        for (auto const& [num, dev] : devices)
        {
            if (dev) dev->atnChanged(atnNowLow); // Pass true if ATN asserted/low
        }

        if (cia2object) cia2object->atnChanged((atnNowLow));

        if (atnNowLow)  // ATN asserted (low) -> Attention state
        {
            currentState = State::ATTENTION;
            currentTalker = nullptr;
            currentListeners.clear();

            // When ATN goes low, peripherals must release CLK/DATA
            if(peripheralDrivesClkLow || peripheralDrivesDataLow)
            {
                peripheralDrivesClkLow = false;
                peripheralDrivesDataLow = false;
                updateBusState(); // Recalculate state if peripheral intentions changed
            }
        }
        else  // ATN released (high) -> Transition state (usually TALK or LISTEN follows)
        {
            // State transition depends on commands received during ATTENTION.
            // For now, just go IDLE, tick() logic will handle actual TALK/LISTEN states.
            currentState = State::IDLE; // Revisit this - may depend on last cmd
        }
    }
}

void IECBUS::setClkLine(bool state)
{
    this->c64DrivesClkLow = !state;
    // Notify the CIA2 of the new state
    updateBusState();
    if(cia2object) cia2object->clkChanged(busLines.clk);
}

void IECBUS::setDataLine(bool state)
{
    this->c64DrivesDataLow = !state;
    updateBusState();
    if(cia2object) cia2object->dataChanged(busLines.data);
}

void IECBUS::setSrqLine(bool state)
{
    bool old = line_srqin;
    line_srqin = state;
    updateBusState();
    // Only notify CIA2 if it actually changed
    if (old != line_srqin) cia2object->srqChanged(line_srqin);
}

void IECBUS::peripheralControlClk(Peripheral* device, bool state)
{
    if (device == nullptr) return;

    // Ensure the peripheral is registered.
    if (devices.find(device->getDeviceNumber()) == devices.end()) return;

    // If no peripheral has yet claimed control, assign the current talker.
    if (currentTalker == nullptr) currentTalker = device;

    // Only allow the designated current talker to drive the CLK line.
    if (device != currentTalker) return;

    // Record the peripheral's intent for the CLK line.
    // (true means the peripheral is asserting LOW)
    peripheralDrivesClkLow = state;

    // If the device releases both lines, clear currentTalker to allow another transfer.
    if (!peripheralDrivesClkLow && !peripheralDrivesDataLow) currentTalker = nullptr;

    // Recalculate the bus state based on both the computer's and peripherals' intentions.
    updateBusState();

   if (cia2object) cia2object->clkChanged(busLines.clk);
}

void IECBUS::peripheralControlData(Peripheral* device, bool state)
{
    if (device == nullptr) return;

    // Check that the device is registered.
    if (devices.find(device->getDeviceNumber()) == devices.end()) return;

    // If no talker is active, assign this device as the current talker.
    if (currentTalker == nullptr) currentTalker = device;

    // Only allow the current talker to drive the DATA line.
    if (device != currentTalker) return;

    // Record the peripheral's intent for the DATA line.
    // (true means the peripheral is asserting LOW)

    peripheralDrivesDataLow = state;

    // Clear currentTalker if the device has released both lines.
    if (!peripheralDrivesClkLow && !peripheralDrivesDataLow) currentTalker = nullptr;

    // Update the final bus state.
    updateBusState();

    if (cia2object) cia2object->dataChanged(busLines.data);
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
