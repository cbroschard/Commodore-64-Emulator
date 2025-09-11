// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Joystick.h"

Joystick::Joystick(int port) :
    port(port),
    state(0xFF)
{
    if (port != 1 && port != 2)
    {
        throw std::invalid_argument("Port must be set to either 1 or 2!");
    }
}

Joystick::~Joystick() = default;

uint8_t Joystick::getState() const
{
    return state;
}

void Joystick::setState(uint8_t newState)
{
    state = newState;
}

bool Joystick::isDirectionPressed(direction dir) const
{
    return (state & dir) == 0;
}

bool Joystick::isButtonPressed() const
{
    return (state & button) == 0;
}

int Joystick::getPort() const
{
    return port;
}
