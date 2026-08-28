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
#include "Serial/RS232Endpoint.h"
#include "Expansion/SwiftLink.h"

SwiftLink::SwiftLink(uint16_t baseAddress) :
    serial(),
    acia(serial),
    nmiLine(nullptr),
    baseAddress(baseAddress)
{
    acia.setBaudMultiplier(2.0);
}

SwiftLink::~SwiftLink() = default;

void SwiftLink::attachEndpoint(RS232Endpoint* endpoint)
{
    acia.attachEndpoint(endpoint);
}

void SwiftLink::detachEndpoint()
{
    acia.detachEndpoint();
}

void SwiftLink::reset()
{
    serial.reset();
    acia.reset();

    if (nmiLine)
        nmiLine->clearNMI(NMILine::SWIFTLINK);
}

void SwiftLink::tick(uint32_t cycles)
{
     acia.tick(cycles);

     if (nmiLine)
     {
        if (acia.getIRQ())
            nmiLine->raiseNMI(NMILine::SWIFTLINK);
        else
            nmiLine->clearNMI(NMILine::SWIFTLINK);
     }
}

uint8_t SwiftLink::read(uint16_t address)
{
    if (!handlesAddress(address))
        return 0xFF;

    return acia.read(static_cast<uint16_t>(address - baseAddress));
}

uint8_t SwiftLink::peek(uint16_t address) const
{
    if (!handlesAddress(address))
        return 0xFF;

    return acia.peek(static_cast<uint16_t>(address - baseAddress));
}

void SwiftLink::write(uint16_t address, uint8_t value)
{
    if (!handlesAddress(address))
        return;

    acia.write(static_cast<uint16_t>(address - baseAddress), value);
}

bool SwiftLink::getIRQ() const
{
    return acia.getIRQ();
}

RS232Device& SwiftLink::getSerial()
{
    return serial;
}

MOS6551& SwiftLink::getACIA()
{
    return acia;
}

bool SwiftLink::handlesAddress(uint16_t address) const
{
    return address >= baseAddress && address <= baseAddress + 3;
}

std::string SwiftLink::dumpDebugOutput(const std::string& subCommand) const
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

    return "Usage: swiftlink [all|general|acia|rs232]";
}

std::string SwiftLink::dumpDebugGeneral() const
{
    std::ostringstream out;

    out << "SwiftLink\n"
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
    << (acia.hasEndpoint() ? "Attached" : "None");

    return out.str();
}

std::string SwiftLink::dumpDebugACIA() const
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

std::string SwiftLink::dumpDebugRS232() const
{
    return serial.debugString();
}
