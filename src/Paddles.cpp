// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Paddles.h"

Paddles::Paddles() = default;


Paddles::~Paddles() = default;

void Paddles::reset()
{
    for (auto& port : ports)
        port = {};
}

void Paddles::setX(int port, uint8_t value)
{
    if (port < 0 || port >= NUM_PORTS)
        return;

    ports[port].x = value;
}

void Paddles::setY(int port, uint8_t value)
{
    if (port < 0 || port >= NUM_PORTS)
        return;

    ports[port].y = value;
}

uint8_t Paddles::getX(int port) const
{
    if (port < 0 || port >= NUM_PORTS)
        return 0x80;

    return ports[port].x;
}

uint8_t Paddles::getY(int port) const
{
    if (port < 0 || port >= NUM_PORTS)
        return 0x80;

    return ports[port].y;
}

void Paddles::setButtonX(int port, bool pressed)
{
    if (port < 0 || port >= NUM_PORTS)
        return;

    ports[port].buttonX = pressed;
}

void Paddles::setButtonY(int port, bool pressed)
{
    if (port < 0 || port >= NUM_PORTS)
        return;

    ports[port].buttonY = pressed;
}

bool Paddles::getButtonX(int port) const
{
    if (port < 0 || port >= NUM_PORTS)
        return false;

    return ports[port].buttonX;
}

bool Paddles::getButtonY(int port) const
{
    if (port < 0 || port >= NUM_PORTS)
        return false;

    return ports[port].buttonY;
}
