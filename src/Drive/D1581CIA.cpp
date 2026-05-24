// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Drive/D1581CIA.h"
#include "Drive/D1581.h"

D1581CIA::D1581CIA() :
    parentPeripheral(nullptr)
{

}

D1581CIA::~D1581CIA() = default;

void D1581CIA::reset()
{
    DriveCIA::reset();

    iecAtnInLow  = false;
    iecClkInLow  = false;
    iecDataInLow = false;
    lastAtnLow   = false;

    setPortAPins(0xFF);
    setPortBPins(makePortBPins());
}

void D1581CIA::setIECInputs(bool atnLow, bool clkLow, bool dataLow)
{
    const bool oldAtnLow = iecAtnInLow;
    const bool atnChanged = (atnLow != oldAtnLow);

    iecAtnInLow  = atnLow;
    iecClkInLow  = clkLow;
    iecDataInLow = dataLow;

    if (atnChanged)
    {
        const bool falling = (!lastAtnLow &&  iecAtnInLow);
        const bool rising  = ( lastAtnLow && !iecAtnInLow);

        if (falling)
            setFlagLine(false);
        else if (rising)
            setFlagLine(true);

        lastAtnLow = iecAtnInLow;
    }

    updateInputPins();

    setSPLine(!iecDataInLow);
    setCNTLine(iecClkInLow);

    applyIECOutputs();
}

void D1581CIA::primeAtnLevel(bool atnLow)
{
    lastAtnLow = atnLow;
}

uint8_t D1581CIA::makePortBPins() const
{
    uint8_t pins = 0xFF;

    if (iecAtnInLow)
        pins |= PRB_ATNIN;
    else
        pins &= static_cast<uint8_t>(~PRB_ATNIN);

    if (iecClkInLow)
        pins |= PRB_CLKIN;
    else
        pins &= static_cast<uint8_t>(~PRB_CLKIN);

    if (iecDataInLow)
        pins |= PRB_DATAIN;
    else
        pins &= static_cast<uint8_t>(~PRB_DATAIN);

    if (auto* d = drive())
    {
        if (d->isDiskWriteProtected())
            pins |= PRB_WRTPRO;
        else
            pins &= static_cast<uint8_t>(~PRB_WRTPRO);
    }

    return pins;
}

void D1581CIA::updateInputPins()
{
    uint8_t portA = 0xFF;
    uint8_t portB = makePortBPins();

    if (auto* d = drive())
    {
        if (!d->isDiskLoaded())
            portA &= static_cast<uint8_t>(~PRA_DRVRDY);

        if (!d->isDiskLoaded())
            portA &= static_cast<uint8_t>(~PRA_DSKCH);

        // Device address switches.
        //
        // Start with the common assumption:
        // device 8 = both switches ON/grounded/0
        // device 9 = DEVSW1 set
        // device 10 = DEVSW2 set
        // device 11 = both set
        const int devOffset = d->getDeviceNumber() - 8;

        if ((devOffset & 0x01) == 0)
            portA &= static_cast<uint8_t>(~PRA_DEVSW1);
        else
            portA |= PRA_DEVSW1;

        if ((devOffset & 0x02) == 0)
            portA &= static_cast<uint8_t>(~PRA_DEVSW2);
        else
            portA |= PRA_DEVSW2;
    }

    setPortAPins(portA);
    setPortBPins(portB);
}

void D1581CIA::applyIECOutputs()
{
    auto* d = drive();
    if (!d)
        return;

    const uint8_t prb  = getPortBOutputRegister();
    const uint8_t ddrb = getDDRB();

    const bool atnAckDataLow =
        iecAtnInLow &&
        ((ddrb & PRB_ATNACK) != 0) &&
        ((prb  & PRB_ATNACK) != 0);

    const bool datOutAssertLow =
        ((ddrb & PRB_DATOUT) != 0) &&
        ((prb  & PRB_DATOUT) != 0);

    const bool clkOutAssertLow =
        ((ddrb & PRB_CLKOUT) != 0) &&
        ((prb  & PRB_CLKOUT) != 0);

    d->peripheralAssertData(atnAckDataLow || datOutAssertLow);
    d->peripheralAssertClk(clkOutAssertLow);
}

void D1581CIA::portAOutputChanged(uint8_t pra, uint8_t ddra)
{
    auto* d = drive();
    if (!d)
        return;

    if (ddra & PRA_SIDE)
    {
        const uint8_t side = (pra & PRA_SIDE) ? 1 : 0;
        d->setCurrentSide(side);
    }

    if (ddra & PRA_MOTOR)
    {
        if ((pra & PRA_MOTOR) == 0)
            d->startMotor();
        else
            d->stopMotor();
    }

    if (ddra & PRA_ACTLED)
    {
        const bool ledOn = (pra & PRA_ACTLED) == 0;
        d->setActivityLed(ledOn);
    }
}

void D1581CIA::portBOutputChanged(uint8_t prb, uint8_t ddrb)
{
    (void)prb;
    (void)ddrb;

    applyIECOutputs();
}

void D1581CIA::irqLineChanged(bool active)
{
    (void)active;

    if (auto* d = drive())
        d->updateIRQ();
}

D1581* D1581CIA::drive() const
{
    return parentPeripheral ? static_cast<D1581*>(parentPeripheral) : nullptr;
}
