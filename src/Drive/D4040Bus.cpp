// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Drive/D4040Bus.h"
#include "Drive/D4040Memory.h"

D4040Bus::D4040Bus(Processor processor) :
    mem(nullptr)
{

}

D4040Bus::~D4040Bus() = default;

uint8_t D4040Bus::read(uint16_t address)
{
    if (!mem)
        return 0xFF;

    if (processor == Processor::FDC6504)
    {
        address &= 0x1FFF;
        return mem->read6504(address);
    }

    return mem->read(address);
}

void D4040Bus::write(uint16_t address, uint8_t value)
{
    if (!mem)
        return;

    if (processor == Processor::FDC6504)
    {
        address &= 0x1FFF;
        mem->write6504(address, value);
        return;
    }

    mem->write(address, value);
}

uint8_t D4040Bus::peek(uint16_t address) const
{
    if (!mem)
        return 0xFF;

    if (processor == Processor::FDC6504)
    {
        address &= 0x1FFF;
        return mem->peek6504(address);
    }

    return mem->peek(address);
}

void D4040Bus::updateRIOTInputs()
{
    if (processor != Processor::DOS6502 || !mem)
        return;

    MOS6532& riot1 = mem->getRIOT1();
    MOS6532& riot2 = mem->getRIOT2();
}

void D4040Bus::updateIEEE488Outputs()
{
    if (processor != Processor::DOS6502 || !mem)
        return;

    const MOS6532& riot1 = mem->getRIOT1();
    const MOS6532& riot2 = mem->getRIOT2();
}
