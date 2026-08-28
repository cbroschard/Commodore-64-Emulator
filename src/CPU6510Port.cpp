// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cassette.h"
#include "CPU6510Port.h"
#include "PLA.h"

CPU6510Port::CPU6510Port() :
    cass(nullptr),
    pla(nullptr),
    dataDirectionRegister(0x2F),
    outputLatch(0x37),
    cassetteSenseLow(false)
{
    applySideEffects();
}

CPU6510Port::~CPU6510Port() = default;

uint8_t CPU6510Port::readDDR() const
{
    return dataDirectionRegister;
}

uint8_t CPU6510Port::readPort() const
{
    const uint8_t outputs = static_cast<uint8_t>(outputLatch & dataDirectionRegister);

    uint8_t inputs = static_cast<uint8_t>(~dataDirectionRegister);

    if (cassetteSenseLow)
        inputs = static_cast<uint8_t>(inputs & ~0x10);
    else
        inputs = static_cast<uint8_t>(inputs | 0x10);

    // Bits 6 and 7 read high.
    inputs = static_cast<uint8_t>(inputs | 0xC0);

    return static_cast<uint8_t>(outputs | inputs);
}

void CPU6510Port::writeDDR(uint8_t value)
{
    dataDirectionRegister = value;
    applySideEffects();
}

void CPU6510Port::writePort(uint8_t value)
{
    outputLatch = value;
    applySideEffects();
}

void CPU6510Port::setCassetteSenseLow(bool low)
{
    cassetteSenseLow = low;
}

uint8_t CPU6510Port::getEffectivePort() const
{
    return computeEffectivePort();
}

uint8_t CPU6510Port::computeEffectivePort() const
{
    const uint8_t invDDR =
        static_cast<uint8_t>(~dataDirectionRegister);

    return static_cast<uint8_t>(
        (outputLatch & dataDirectionRegister) | invDDR);
}

void CPU6510Port::applySideEffects()
{
    const uint8_t effective = computeEffectivePort();

    // Bit 5 low => cassette motor ON.
    const bool motorOn = (effective & 0x20) == 0;

    if (cass)
    {
        if (motorOn)
            cass->startMotor();
        else
            cass->stopMotor();
    }

    // Bits 0-2 control the C64 memory configuration.
    if (pla)
        pla->updateMemoryControlRegister(effective & 0x07);
}
