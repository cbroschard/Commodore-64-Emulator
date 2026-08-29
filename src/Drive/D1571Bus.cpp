// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Drive/D1571Bus.h"
#include "Drive/D1571Memory.h"

D1571Bus::D1571Bus() :
    mem(nullptr)
{
}

D1571Bus::~D1571Bus() = default;

uint8_t D1571Bus::read(uint16_t address)
{
    if (!mem)
        return 0xFF;

    return mem->read(address);
}

void D1571Bus::write(uint16_t address, uint8_t value)
{
    if (!mem)
        return;

    mem->write(address, value);
}

uint8_t D1571Bus::peek(uint16_t address) const
{
    if (!mem)
        return 0xFF;

    return mem->peek(address);
}
