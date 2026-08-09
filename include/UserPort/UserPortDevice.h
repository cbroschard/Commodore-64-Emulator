// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef USERPORTDEVICE_H
#define USERPORTDEVICE_H

#include <cstdint>
#include <string>
#include "Serial/RS232Device.h"

class UserPortDevice
{
public:
    virtual ~UserPortDevice() = default;

    virtual void reset() {}
    virtual void tick(uint32_t cyclesElapsed) {}

    virtual void portAChanged(uint8_t value, uint8_t ddr) {}
    virtual void portBChanged(uint8_t value, uint8_t ddr) {}

    virtual uint8_t readPortB() const { return 0xFF; }

    virtual std::string debugString() const { return "  Device: generic\n"; }
    virtual std::string debugRS232String() const { return "RS-232 not supported by attached User Port device\n"; }
    virtual std::string selfTestRS232(uint8_t value,  RS232Device::Parity parity) { return "RS-232 not supported by attached User Port device\n"; }
    virtual std::string selfTestRS232Multi() { return "RS-232 not supported by attached User Port device\n"; }
    virtual std::string selfTestUserPortRS232Formats() { return "RS-232 not supported by attached User Port device\n"; }
};

#endif // USERPORTDEVICE_H
