// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Drive/D1581.h"
#include "Drive/D1581CIA.h"

D1581CIA::D1581CIA() :
    parentPeripheral(nullptr)
{

}

D1581CIA::~D1581CIA() = default;

void D1581CIA::setIECInputs(bool atnLow, bool clkLow, bool dataLow)
{
    const bool atnChanged = (atnLow != iecAtnInLow);

    iecAtnInLow  = atnLow;
    iecClkInLow  = clkLow;
    iecDataInLow = dataLow;

    updateInputPins();

    // Preserve the working polarity from your old DriveCIA:
    // IEC CLK low  -> CIA CNT high
    // IEC DATA low -> CIA SP low
    setCNTLine(iecClkInLow);
    setSPLine(!iecDataInLow);

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

    applyIECOutputs();
}

void D1581CIA::primeAtnLevel(bool atnLow)
{
    lastAtnLow = atnLow;
}

uint8_t D1581CIA::makePortBPins() const
{
    uint8_t pins = 0xFF;

    // Current working polarity from old DriveCIA:
    // physical IEC LOW  => CIA PRB input bit 1
    // physical IEC HIGH => CIA PRB input bit 0

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

    return pins;
}

void D1581CIA::updateInputPins()
{
    uint8_t portA = 0xFF;
    uint8_t portB = makePortBPins();

    // Later migrate old sampleA_1581 logic here:
    // drive ready, disk change, device switches, etc.

    setPortAPins(portA);
    setPortBPins(portB);
}

void D1581CIA::applyIECOutputs()
{
    if (!parentPeripheral)
        return;

    auto* drive = static_cast<D1581*>(parentPeripheral);

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

    const bool driveDataLow = atnAckDataLow || datOutAssertLow;
    const bool driveClkLow  = clkOutAssertLow;

    drive->peripheralAssertData(driveDataLow);
    drive->peripheralAssertClk(driveClkLow);
}

void D1581CIA::portAOutputChanged(uint8_t pra, uint8_t ddra)
{
    (void)pra;
    (void)ddra;

    // Later migrate old applyA_1581 logic here.
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

    if (!parentPeripheral)
        return;

    auto* drive = static_cast<D1581*>(parentPeripheral);
    drive->updateIRQ();
}
