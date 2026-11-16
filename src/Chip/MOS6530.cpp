// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Chip/MOS6530.h"

MOS6530::MOS6530()
{
    ram.fill(0x00);
    reset();
}

MOS6530::~MOS6530() = default;

void MOS6530::reset()
{

}

void MOS6530::save(StateWriter& wrtr) const
{

}

bool MOS6530::load(StateReader& rdr)
{
    return true;
}

uint8_t MOS6530::read(uint8_t address)
{
    return 0xFF;
}

void MOS6530::tick(uint32_t cycles)
{

}

void MOS6530::write(uint8_t address, uint8_t value)
{

}

uint8_t MOS6530::peek(uint8_t address) const
{
    return 0xFF;
}


uint8_t MOS6530::readRAM(uint8_t address) const
{
    return ram[address & 0x3F];
}

void MOS6530::writeRAM(uint8_t address, uint8_t value)
{
    ram[address & 0x3F] = value;
}
