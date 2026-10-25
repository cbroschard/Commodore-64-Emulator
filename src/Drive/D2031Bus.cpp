// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Drive/D2031Bus.h"
#include "Drive/D2031Memory.h"

D2031Bus::D2031Bus() :
    mem(nullptr)
{

}

D2031Bus::~D2031Bus() = default;

uint8_t D2031Bus::read(uint16_t address)
{
    if (!mem)
        return 0xFF;

    return mem->read(address);
}

void D2031Bus::write(uint16_t address, uint8_t value)
{
    if (!mem)
        return;

    mem->write(address, value);
}

uint8_t D2031Bus::peek(uint16_t address) const
{
    if (!mem)
        return 0xFF;

    return mem->peek(address);
}
