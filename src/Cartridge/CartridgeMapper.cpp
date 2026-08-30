// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Bus.h"
#include "Cartridge/CartridgeMapper.h"
#include "Cartridge.h"
#include "Memory.h"

CartridgeMapper::CartridgeMapper() :
    bus(nullptr),
    cart(nullptr)
{

}

CartridgeMapper::~CartridgeMapper() = default;

uint8_t CartridgeMapper::peek(uint16_t address) const
{
    (void)address;

    return cart ? cart->sampleDataBus() : 0xFF;
}

bool CartridgeMapper::readDrivesBus(uint16_t address) const
{
    (void)address;
    return true;
}

bool CartridgeMapper::cpuMemoryHandledByMapper(uint16_t address) const
{
    (void)address;
    return false;
}
