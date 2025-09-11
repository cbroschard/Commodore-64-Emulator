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
    rxd(true),
    cts(true),
    dcd(true),
    ri(true)
{

}

RS232Device::~RS232Device() = default;

void RS232Device::setDTR(bool state)
{
    dtr = state;
    if (peer)
    {
        peer->dsr = state;
    }
}

void RS232Device::setRTS(bool state)
{
    rts = state;
    if (peer)
    {
        peer->cts = state;
    }
}
