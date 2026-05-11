// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "RS232Device.h"

RS232Device::RS232Device() :
    dtr(true),
    dsr(true),
    rts(true),
    txd(true),
    rxd(true),
    cts(true),
    dcd(true),
    ri(true)
{

}

RS232Device::~RS232Device() = default;

void RS232Device::setTXD(bool state)
{
    txd = state;

    if (!peer)
        return;

    peer->rxd = state;
}

void RS232Device::setDTR(bool state)
{
    dtr = state;

    if (!peer)
        return;

    // Practical null-modem/modem-present behavior.
    peer->dsr = state;
    peer->dcd = state;
}

void RS232Device::setRTS(bool state)
{
    rts = state;

    if (!peer)
        return;

    peer->cts = state;
}

std::string RS232Device::debugString() const
{
    std::ostringstream out;

    out << "RS232 Device:\n";
    out << "  Outputs: "
        << "TXD=" << (txd ? "H" : "L") << " "
        << "DTR=" << (dtr ? "H" : "L") << " "
        << "RTS=" << (rts ? "H" : "L") << "\n";

    out << "  Inputs:  "
        << "RXD=" << (rxd ? "H" : "L") << " "
        << "DSR=" << (dsr ? "H" : "L") << " "
        << "CTS=" << (cts ? "H" : "L") << " "
        << "DCD=" << (dcd ? "H" : "L") << " "
        << "RI="  << (ri  ? "H" : "L") << "\n";

    out << "  Peer: " << (peer ? "attached" : "none") << "\n";

    return out.str();
}
