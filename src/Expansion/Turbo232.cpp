// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include <iomanip>
#include <sstream>
#include "NMILine.h"
#include "Expansion/Turbo232.h"

Turbo232::Turbo232(uint16_t baseAddress) :
    acia(serial),
    nmiLine(nullptr),
    baseAddress(baseAddress),
    enhancedSpeedRegister(0x00)
{
    acia.setBaudMultiplier(2.0);
}

Turbo232::~Turbo232() = default;

void Turbo232::attachEndpoint(RS232Endpoint* endpoint)
{
    acia.attachEndpoint(endpoint);
}

void Turbo232::detachEndpoint()
{
    acia.detachEndpoint();
}

void Turbo232::reset()
{
    serial.reset();
    acia.reset();

    enhancedSpeedRegister = 0x00;
    updateBaudRate();

    if (nmiLine)
        nmiLine->clearNMI(NMILine::TURBO232);
}

void Turbo232::tick(uint32_t cycles)
{
    acia.tick(cycles);

    if (nmiLine)
    {
        if (acia.getIRQ())
            nmiLine->raiseNMI(NMILine::TURBO232);
        else
            nmiLine->clearNMI(NMILine::TURBO232);
    }
}

uint8_t Turbo232::read(uint16_t address)
{
    if (!handlesAddress(address))
        return 0xFF;

    const uint16_t reg = static_cast<uint16_t>(address - baseAddress) & 0x07;

    if (reg <= 3)
        return acia.read(reg);

    if (reg == 7)
        return readEnhancedSpeedRegister();

    return 0xFF;
}

void Turbo232::write(uint16_t address, uint8_t value)
{
    if (!handlesAddress(address))
        return;

    const uint16_t reg = static_cast<uint16_t>(address - baseAddress) & 0x07;

    if (reg <= 3)
    {
        acia.write(reg, value);

        if (reg == 3)
            updateBaudRate();

        return;
    }

    if (reg == 7)
        writeEnhancedSpeedRegister(value);
}

bool Turbo232::handlesAddress(uint16_t address) const
{
    return address >= baseAddress && address < static_cast<uint16_t>(baseAddress + 0x20);
}

uint8_t Turbo232::readEnhancedSpeedRegister() const
{
    uint8_t value = enhancedSpeedRegister & 0x03;

    if (enhancedModeEnabled())
        value |= 0x04;

    return value;
}

void Turbo232::writeEnhancedSpeedRegister(uint8_t value)
{
    if (!enhancedModeEnabled())
        return;

    enhancedSpeedRegister = value & 0x03;

    updateBaudRate();
}

uint32_t Turbo232::decodeEnhancedBaud() const
{
    switch (enhancedSpeedRegister & 0x03)
    {
        case 0x00:
            return 230400;

        case 0x01:
            return 115200;

        case 0x02:
            return 57600;

        default:
            return 0;
    }
}

bool Turbo232::enhancedModeEnabled() const
{
    return acia.isExternalBaudSelected();
}

void Turbo232::updateBaudRate()
{
    if (enhancedModeEnabled())
    {
        const uint32_t baud = decodeEnhancedBaud();

        if (baud != 0)
            serial.setBaud(baud);
    }
}

std::string Turbo232::dumpDebugOutput(const std::string& subCommand) const
{
    if (subCommand.empty() || subCommand == "all")
    {
        std::ostringstream out;

        out << dumpDebugGeneral()
            << "\n"
            << dumpDebugACIA()
            << "\n"
            << dumpDebugRS232();

        return out.str();
    }

    if (subCommand == "general")
        return dumpDebugGeneral();

    if (subCommand == "acia")
        return dumpDebugACIA();

    if (subCommand == "rs232")
        return dumpDebugRS232();

    return "Usage: turbo232 [all|general|acia|rs232]";
}

std::string Turbo232::dumpDebugGeneral() const
{
    std::ostringstream out;

    out << "Turbo232\n"
        << "Base Address: $"
        << std::hex
        << std::uppercase
        << std::setw(4)
        << std::setfill('0')
        << baseAddress
        << std::dec
        << "\n"
        << "ACIA IRQ: "
        << (acia.getIRQ() ? "Active" : "Inactive")
        << "\n"
        << "Endpoint: "
        << (acia.hasEndpoint() ? "Attached" : "None")
        << "\n"
        << "Enhanced Mode: "
        << (enhancedModeEnabled() ? "Yes" : "No")
        << "\n"
        << "Enhanced Speed Register: $"
        << std::hex
        << std::uppercase
        << std::setw(2)
        << std::setfill('0')
        << static_cast<unsigned>(readEnhancedSpeedRegister())
        << std::dec
        << "\n"
        << "Baud: "
        << serial.getConfig().baud;

    return out.str();
}

std::string Turbo232::dumpDebugACIA() const
{
    std::ostringstream out;

    out << "MOS6551 ACIA\n"
        << "Status:  $"
        << std::hex
        << std::uppercase
        << std::setw(2)
        << std::setfill('0')
        << static_cast<unsigned>(acia.getStatusRegister())
        << "\n"
        << "Command: $"
        << std::setw(2)
        << static_cast<unsigned>(acia.getCommandRegister())
        << "\n"
        << "Control: $"
        << std::setw(2)
        << static_cast<unsigned>(acia.getControlRegister())
        << std::dec
        << "\n"
        << "IRQ: "
        << (acia.getIRQ() ? "Active" : "Inactive");

    return out.str();
}

std::string Turbo232::dumpDebugRS232() const
{
    return serial.debugString();
}
