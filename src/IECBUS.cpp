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

        std::cout << "[BUS] setAtnLine(" << state
              << ") oldATN=" << oldATNState
              << " newATN=" << busLines.atn
              << " atnNowLow=" << atnNowLow
              << "\n";

        // Notify all peripherals of the change
        for (auto const& [num, dev] : devices)
        {
            std::cout << "[IECBUS] ATN notify device #" << num
              << " ptr=" << dev << "\n";
            if (dev) dev->atnChanged(atnNowLow); // Pass true if ATN asserted/low
        }

        if (cia2object) cia2object->atnChanged((atnNowLow));

        if (atnNowLow)
        {
            currentState = State::ATTENTION;
            currentTalker = nullptr;
            currentListeners.clear();

            // Release CLK only; don't stomp DATA here
            if (peripheralDrivesClkLow)
            {
                peripheralDrivesClkLow = false;
                updateBusState();
            }
        }
        else
        {
            currentState = State::IDLE;
        }
        updateBusState();
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

    std::cout << "[IECBUS] setClkLine from C64: state=" << state
              << " oldBusClk=" << oldClk
              << " newBusClk=" << busLines.clk
              << " ATN=" << (busLines.atn ? "H" : "L")
              << "\n";

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

    std::cout << "[BUS] setDataLine(" << state
              << ") -> bus DATA=" << int(busLines.data) << "\n";

    if (cia2object)
        cia2object->dataChanged(busLines.data);

    for (auto const& [num, dev] : devices)
    {
        if (dev) dev->dataChanged(busLines.data);
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

void IECBUS::peripheralControlClk(Peripheral* device, bool state)
{
    if (!device) return;
    if (devices.find(device->getDeviceNumber()) == devices.end()) return;

    // Rule 1: If ATN is LOW (Attention), no peripheral can drive CLK.
    if (!busLines.atn /* && currentState == State::ATTENTION */)
    {
        std::cout << "[IECBUS] peripheralControlClk from dev#"
                  << int(device->getDeviceNumber())
                  << " blocked (ATN low)\n";
        return; // C64 owns the clock during ATTENTION
    }

    std::cout << "[IECBUS] peripheralControlClk from dev#"
              << int(device->getDeviceNumber())
              << " state=" << state
              << " busClk(before)=" << busLines.clk
              << "\n";

    // Rule 2: Any peripheral (Listener or Talker) can pull a line LOW (state == true).
    // This allows the Listener (drive) to assert CLK LOW for its ACK.
    if (state)
    {
        peripheralDrivesClkLow = true;
    }
    else // Peripheral wants to release CLK high (state == false)
    {
        // Rule 3: Only the currently established Talker can command a line to be released/HIGH.
        if (currentTalker == nullptr) currentTalker = device;
        if (device != currentTalker) return; // Block unauthorized release

        peripheralDrivesClkLow = false;

        // Only clear Talker status if both CLK and DATA were released by the Talker
        if (!peripheralDrivesClkLow && !peripheralDrivesDataLow)
            currentTalker = nullptr;
    }

    updateBusState();
    if (cia2object) cia2object->clkChanged(busLines.clk);
    for (auto& [num, dev] : devices)
    {
        if (dev)
            dev->clkChanged(!busLines.clk);
    }
}

void IECBUS::peripheralControlData(Peripheral* device, bool state)
{
    if (!device) return;
    if (devices.find(device->getDeviceNumber()) == devices.end()) return;

    // During ATTENTION (ATN low), *allow* any device to pulse DATA for presence/byte ACKs.
    if (!busLines.atn || currentState == State::ATTENTION)
    {
        peripheralDrivesDataLow = state;     // true => drive DATA low
        updateBusState();
        if (cia2object) cia2object->dataChanged(busLines.data);
        for (auto const& [num,dev] : devices)
        {
            if (dev) dev->dataChanged(busLines.data);
        }
        std::cout << "[BUS] periph DATA=" << state << " while ATN low -> line now " << busLines.data << "\n";
        return;
    }

    // Normal talking rules (ATN high)
    if (currentTalker == nullptr) currentTalker = device;
    if (device != currentTalker) return;

    peripheralDrivesDataLow = state;
    if (!peripheralDrivesClkLow && !peripheralDrivesDataLow) currentTalker = nullptr;

    updateBusState();
    if (cia2object) cia2object->dataChanged(busLines.data);
    for (auto const& [num,dev] : devices)
    {
        if (dev) dev->dataChanged(busLines.data);
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
    std::cout << "[IECBUS] registerDevice " << deviceNumber
          << " at ptr=" << device << "\n";
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

void IECBUS::updateBusState()
{
    busLines.updateLineState(c64DrivesClkLow, c64DrivesDataLow, peripheralDrivesClkLow, peripheralDrivesDataLow,
        c64DrivesAtnLow, peripheralDrivesAtnLow);

     std::cout << "[BUS] state=" << static_cast<int>(currentState)
              << " C64ATNlow=" << c64DrivesAtnLow
              << " PeriphATNlow=" << peripheralDrivesAtnLow
              << " busATN=" << busLines.atn
              << "\n";
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
