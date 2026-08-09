// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include <sstream>
#include "UserPort/UserPort.h"
#include "UserPort/UserPortDevice.h"

UserPort::UserPort() :
    device(nullptr)
{

}

UserPort::~UserPort()
{

}

void UserPort::reset()
{
    if (device)
        device->reset();
}

void UserPort::tick(uint32_t cyclesElapsed)
{
    if (device)
        device->tick(cyclesElapsed);
}

void UserPort::portAChanged(uint8_t value, uint8_t ddr)
{
    if (device)
        device->portAChanged(value, ddr);
}

void UserPort::portBChanged(uint8_t value, uint8_t ddr)
{
    if (device)
        device->portBChanged(value, ddr);
}

uint8_t UserPort::readPortB() const
{
    return device ? device->readPortB() : 0xFF;
}

std::string UserPort::debugString() const
{
    std::ostringstream out;

    out << "User Port:\n";

    if (!device)
    {
        out << "  Device: none attached\n";
        return out.str();
    }

    out << device->debugString();

    return out.str();
}

std::string UserPort::debugRS232String() const
{
    if (!device)
        return "User Port: no device attached\n";

    return device->debugRS232String();
}
