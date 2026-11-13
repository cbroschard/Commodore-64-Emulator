// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Chip/MOS6532.h"

MOS6532::MOS6532()
{
    ram.fill(0x00);
    reset();
}

MOS6532::~MOS6532() = default;

void MOS6532::reset()
{
    // I/O registers
    registers.portA = 0x00;
    registers.ddrA = 0x00;
    registers.portB = 0x00;
    registers.ddrB = 0x00;

    // Timer
    registers.timer = 0x00;
    timerPrescaler = 1;
    timerDivider = 0;
    timerRunning = false;
    timerUnderflowed = false;

    // Interrupts
    registers.interruptFlags = 0x00;
    registers.timerIRQEnabled = false;
    registers.pa7IRQEnabled = false;
    registers.pa7PositiveEdge = false;

    // External input defaults
    inputA = 0xFF;
    inputB = 0xFF;

    // Initialize edge detection from current input
    previousPA7 = (inputA & 0x80) != 0;
}

void MOS6532::tick(uint32_t cycles)
{

}

uint8_t MOS6532::readRegister(uint16_t address)
{
    return 0xFF;
}

void MOS6532::writeRegister(uint16_t address, uint8_t value)
{

}
