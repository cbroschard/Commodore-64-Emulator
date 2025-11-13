// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Drive/D1581.h"
#include "Drive/D1581Memory.h"

D1581Memory::D1581Memory() :
    logger(nullptr),
    parentPeripheral(nullptr),
    setLogging(false),
    lastBus(0xFF)
{
    D1581RAM.resize(RAM_SIZE);
    D1581ROM.resize(ROM_SIZE);
}

D1581Memory::~D1581Memory() = default;

void D1581Memory::attachPeripheralInstance(Peripheral* parentPeripheral)
{
    this->parentPeripheral = parentPeripheral;

    cia.attachPeripheralInstance(parentPeripheral);
    fdc.attachPeripheralInstance(parentPeripheral);

    auto* host = dynamic_cast<FloppyControllerHost*>(parentPeripheral);
    fdc.attachFloppyeControllerHostInstance(host);
}

void D1581Memory::reset()
{
    // Clear RAM
    std::fill(D1581RAM.begin(), D1581RAM.end(), 0x00);

    // Disable ML Monitor logging
    setLogging = false;

    // Default return for open bus
    lastBus = 0xFF;
}

void D1581Memory::tick()
{

}

uint8_t D1581Memory::read(uint16_t address)
{
    uint8_t value; // Hold for updating last bus

    if (address >= RAM_START && address <= RAM_END)
    {
        value = D1581RAM[address - RAM_START];
    }
    if (address >= CIA_START && address <= CIA_END)
    {
        uint8_t offset = (address - CIA_START) & 0x000F;
        value = cia.readRegister(offset);
    }
    if (address >= FDC_START && address <= FDC_END)
    {
        uint8_t offset = (address - FDC_START);
        value = fdc.readRegister(offset);
    }

    lastBus = value;
    return value;
}

void D1581Memory::write(uint16_t address, uint8_t value)
{

}
