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

bool UserPort::getFlag2() const
{
    return device ? device->getFlag2() : true;
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

std::string UserPort::selfTestRS232(uint8_t value, RS232Device::Parity parity)
{
    if (!device)
        return "User Port: no device attached\n";

    return device->selfTestRS232(value, parity);
}

std::string UserPort::selfTestRS232Multi()
{
    if (!device)
        return "User Port: no device attached\n";

    return device->selfTestRS232Multi();
}

std::string UserPort::selfTestUserPortRS232Formats()
{
    if (!device)
        return "User Port: no device attached\n";

    return device->selfTestUserPortRS232Formats();
}

std::string UserPort::selfTestUserPortRS232FlowControl()
{
    if (!device)
        return "User Port: no device attached\n";

    return device->selfTestUserPortRS232FlowControl();
}

std::string UserPort::selfTestUserPortRS232Errors()
{
    if (!device)
        return "User Port: no device attached\n";

    return device->selfTestUserPortRS232Errors();
}
