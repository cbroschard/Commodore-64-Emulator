// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.#include "Drive/D1581Bus.h"
#include "Drive/D1581Bus.h"
#include "Drive/D1581Memory.h"

D1581Bus::D1581Bus() :
    mem(nullptr)
{
}

D1581Bus::~D1581Bus() = default;

uint8_t D1581Bus::read(uint16_t address)
{
    if (!mem)
        return 0xFF;

    return mem->read(address);
}

void D1581Bus::write(uint16_t address, uint8_t value)
{
    if (!mem)
        return;

    mem->write(address, value);
}

uint8_t D1581Bus::peek(uint16_t address) const
{
    if (!mem)
        return 0xFF;

    return mem->peek(address);
}
