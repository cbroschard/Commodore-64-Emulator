// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "UserPort/UserPortRS232Adapter.h"
#include "Serial/RS232Device.h"

UserPortRS232Adapter::UserPortRS232Adapter() :
    rs232Device(nullptr)
{

}

UserPortRS232Adapter::~UserPortRS232Adapter()
{

}

void UserPortRS232Adapter::reset()
{
    portAValue = 0xFF;
    portADDR   = 0x00;

    portBValue = 0xFF;
    portBDDR   = 0x00;

    if (rs232Device)
        rs232Device->reset();
}

void UserPortRS232Adapter::tick(uint32_t cyclesElapsed)
{
    if (rs232Device)
        rs232Device->tick(cyclesElapsed);
}

void UserPortRS232Adapter::portAChanged(uint8_t value, uint8_t ddr)
{
    portAValue = value;
    portADDR   = ddr;

    if (!rs232Device)
        return;

    if (ddr & TXD_MASK)
        rs232Device->setTXD((value & TXD_MASK) != 0);
}

void UserPortRS232Adapter::portBChanged(uint8_t value, uint8_t ddr)
{
    portBValue = value;
    portBDDR   = ddr;

    if (!rs232Device)
        return;

    if (ddr & RTS_MASK)
        rs232Device->setRTS((value & RTS_MASK) != 0);

    if (ddr & DTR_MASK)
        rs232Device->setDTR((value & DTR_MASK) != 0);
}

uint8_t UserPortRS232Adapter::readPortB() const
{
    if (!rs232Device)
        return 0xFF;

    uint8_t value = 0xFF;

    if (!rs232Device->getRXD()) value &= ~RXD_MASK;
    if (!rs232Device->getRI())  value &= ~RI_MASK;
    if (!rs232Device->getDCD()) value &= ~DCD_MASK;
    if (!rs232Device->getCTS()) value &= ~CTS_MASK;
    if (!rs232Device->getDSR()) value &= ~DSR_MASK;

    return value;
}

std::string UserPortRS232Adapter::debugString() const
{
    std::ostringstream out;

    out << "  Device: RS-232 Adapter\n";

    out << "\n";
    out << "  C64 User Port Mapping\n";
    out << "    PA2 TXD: "
        << ((portADDR & TXD_MASK) ? "output" : "input")
        << " latch="
        << ((portAValue & TXD_MASK) ? "H" : "L")
        << "\n";

    out << "    PB1 RTS: "
        << ((portBDDR & RTS_MASK) ? "output" : "input")
        << " latch="
        << ((portBValue & RTS_MASK) ? "H" : "L")
        << "\n";

    out << "    PB2 DTR: "
        << ((portBDDR & DTR_MASK) ? "output" : "input")
        << " latch="
        << ((portBValue & DTR_MASK) ? "H" : "L")
        << "\n";

    if (rs232Device)
    {
        out << "\n";
        out << rs232Device->debugString();
    }
    else
    {
        out << "\n  RS232 Device: none attached\n";
    }

    return out.str();
}

std::string UserPortRS232Adapter::debugRS232String() const
{
    if (!rs232Device)
        return "RS232 Device: none attached\n";

    return rs232Device->debugString();
}

std::string UserPortRS232Adapter::selfTestRS232(uint8_t value)
{
    if (!rs232Device)
        return "RS232 Device: none attached\n";

    return rs232Device->selfTest(value);
}
