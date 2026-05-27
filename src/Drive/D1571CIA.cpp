// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Drive/D1571CIA.h"
#include "Drive/D1571.h"

D1571CIA::D1571CIA() :
    parentPeripheral(nullptr)
{

}

D1571CIA::~D1571CIA() = default;

void D1571CIA::reset()
{
    DriveCIA::reset();

    iecAtnInLow  = false;
    iecClkInLow  = false;
    iecDataInLow = false;
    lastAtnLow   = false;

    setPortAPins(0xFF);
    setPortBPins(0xFF);
}

void D1571CIA::setIECInputs(bool atnLow, bool clkLow, bool dataLow)
{
    iecAtnInLow  = atnLow;
    iecClkInLow  = clkLow;
    iecDataInLow = dataLow;

    updateInputPins();
}

void D1571CIA::primeAtnLevel(bool atnLow)
{
    lastAtnLow = atnLow;
}

uint8_t D1571CIA::makePortBPins() const
{
    return 0xFF;
}

void D1571CIA::portAOutputChanged(uint8_t pra, uint8_t ddra)
{
    (void)pra;
    (void)ddra;
}

void D1571CIA::portBOutputChanged(uint8_t prb, uint8_t ddrb)
{
    (void)prb;
    (void)ddrb;
}

void D1571CIA::irqLineChanged(bool active)
{
    (void)active;

    if (auto* d = drive())
        d->updateIRQ();
}

void D1571CIA::updateInputPins()
{
    setPortAPins(0xFF);
    setPortBPins(0xFF);
}

void D1571CIA::applyIECOutputs()
{
}

D1571* D1571CIA::drive() const
{
    return parentPeripheral ? static_cast<D1571*>(parentPeripheral) : nullptr;
}

DriveCIABase::ciaIECDecodeView D1571CIA::getIECDecodeView() const
{
    DriveCIABase::ciaIECDecodeView v;
    v.available = false;
    v.modelName = "1571";
    return v;
}
