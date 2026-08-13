// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Expansion/SwiftLink.h"
#include "NMILine.h"

SwiftLink::SwiftLink(uint16_t baseAddress) :
    serial(),
    acia(serial),
    nmiLine(nullptr),
    baseAddress(baseAddress)
{
    acia.setBaudMultiplier(2.0);
}

SwiftLink::~SwiftLink() = default;

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
