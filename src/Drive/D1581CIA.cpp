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
    parentPeripheral(nullptr),
    iecAtnInLow(false),
    iecClkInLow(false),
    iecDataInLow(false),
    iecSrqInLow(false),
    lastAtnLow(false),
    updating(false)
{

}

D1581CIA::~D1581CIA() = default;

void D1581CIA::reset()
{
    DriveCIA::reset();

    iecAtnInLow  = false;
    iecClkInLow  = false;
    iecDataInLow = false;
    iecSrqInLow  = false;
    lastAtnLow   = false;

    updating     = false;

    setPortAPins(0xFF);
    setPortBPins(0xFF);
}

void D1581CIA::setIECInputs(bool atnLow, bool clkLow, bool dataLow, bool srqLow)
{
    const bool oldAtnLow = iecAtnInLow;
    const bool atnChanged = (atnLow != oldAtnLow);

    iecAtnInLow  = atnLow;
    iecClkInLow  = clkLow;
    iecDataInLow = dataLow;
    iecSrqInLow  = srqLow;

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

    setSPLine(!iecDataInLow);
    setCNTLine(!iecSrqInLow);

    refreshIECPortState();
}

void D1581CIA::refreshIECPortState()
{
    if (updating)
        return;

    updating = true;

    applyIECOutputs();
    updateInputPins();

    updating = false;
}

void D1581CIA::primeAtnLevel(bool atnLow)
{
    lastAtnLow = atnLow;
}

uint8_t D1581CIA::makePortBPins() const
{
    uint8_t pins = 0xFF;

    const auto o = decodeIECOutputs();

    const bool resolvedAtnLow  = iecAtnInLow;
    const bool resolvedClkLow  = iecClkInLow  || o.clkOutAssertLow;
    const bool resolvedDataLow = iecDataInLow || o.atnAckDataLow || o.datOutAssertLow;

    if (resolvedAtnLow)
        pins |= PRB_ATNIN;
    else
        pins &= static_cast<uint8_t>(~PRB_ATNIN);

    if (resolvedClkLow)
        pins |= PRB_CLKIN;
    else
        pins &= static_cast<uint8_t>(~PRB_CLKIN);

    if (resolvedDataLow)
        pins |= PRB_DATAIN;
    else
        pins &= static_cast<uint8_t>(~PRB_DATAIN);

    if (auto* d = drive())
    {
        if (d->isDiskWriteProtected())
            pins &= static_cast<uint8_t>(~PRB_WRTPRO);
        else
            pins |= PRB_WRTPRO;
    }

    return pins;
}

void D1581CIA::updateInputPins()
{
    uint8_t portA = 0xFF;
    uint8_t portB = makePortBPins();

    if (auto* d = drive())
    {
        // PA1 /DRVRDY or /RDY: active low.
        // Ready should assert low when disk is loaded and motor is on.
        if (d->isDiskLoaded() && d->isMotorOn())
            portA &= static_cast<uint8_t>(~PRA_DRVRDY);
        else
            portA |= PRA_DRVRDY;

        portA |= PRA_DSKCH;

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

    const auto o = decodeIECOutputs();

    d->peripheralAssertData(o.finalDataLow);
    d->peripheralAssertClk(o.finalClkLow);
}

void D1581CIA::portAOutputChanged(uint8_t pra, uint8_t ddra)
{
    auto* d = drive();
    if (!d)
        return;

    if (ddra & PRA_SIDE)
    {
        const uint8_t outA = static_cast<uint8_t>(pra & ddra);
        const uint8_t side = (outA & PRA_SIDE) ? 1 : 0;
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

    refreshIECPortState();
}

void D1581CIA::portBOutputChanged(uint8_t prb, uint8_t ddrb)
{
    (void)prb;
    (void)ddrb;

    refreshIECPortState();
}

void D1581CIA::irqLineChanged(bool active)
{
    (void)active;

    if (auto* d = drive())
        d->updateIRQ();
}

void D1581CIA::serialOutputBit(bool bit)
{
    auto* d = drive();
    if (!d)
        return;

    // IEC lines are active-low.
    // For a serial data bit:
    // bit 1 -> release DATA
    // bit 0 -> pull DATA low
    d->peripheralAssertData(!bit);

#ifdef Debug
    std::cout << "[1581 CIA SERIAL BIT] bit=" << (bit ? 1 : 0)
              << " DATA=" << (bit ? "H" : "L")
              << " PRB=$" << hex2(getPortBOutputRegister())
              << " DDRB=$" << hex2(getDDRB())
              << "\n";
#endif
}

void D1581CIA::serialOutputClockPulse()
{
    auto* d = drive();
    if (!d)
        return;

    // First simple test: pulse SRQ low.
    // This may need timing/stretching later.
    d->peripheralAssertSrq(true);
    d->peripheralAssertSrq(false);

#ifdef Debug
    std::cout << "[1581 CIA SERIAL CLK SRQ PULSE]\n";
#endif
}

void D1581CIA::serialOutputFinished()
{
    auto* d = drive();
    if (d)
    {
        d->peripheralAssertData(false);
        d->peripheralAssertSrq(false);
    }

#ifdef Debug
    std::cout << "[1581 CIA SERIAL DONE]\n";
#endif
}

D1581* D1581CIA::drive() const
{
    return parentPeripheral ? static_cast<D1581*>(parentPeripheral) : nullptr;
}

DriveCIABase::ciaIECDecodeView D1581CIA::getIECDecodeView() const
{
    DriveCIABase::ciaIECDecodeView v;

    v.available = true;
    v.modelName = "1581";

    v.pr  = getPortBOutputRegister();
    v.ddr = getDDRB();

    v.rawPortAPins = getPortAPinsDebug();
    v.rawPortBPins = getPortBPinsDebug();

    v.atnInLow  = iecAtnInLow;
    v.clkInLow  = iecClkInLow;
    v.dataInLow = iecDataInLow;
    v.srqInLow  = iecSrqInLow;

    const auto o = decodeIECOutputs();

    v.busDirOutput    = o.busDirOutput;
    v.atnAckDataLow   = o.atnAckDataLow;
    v.datOutAssertLow = o.datOutAssertLow;
    v.clkOutAssertLow = o.clkOutAssertLow;
    v.finalDataLow    = o.finalDataLow;
    v.finalClkLow     = o.finalClkLow;

    v.resolvedAtnLow  = iecAtnInLow;
    v.resolvedClkLow  = iecClkInLow  || v.clkOutAssertLow;
    v.resolvedDataLow = iecDataInLow || v.atnAckDataLow || v.datOutAssertLow;

    v.finalDataLow = v.atnAckDataLow || v.datOutAssertLow;
    v.finalClkLow  = v.clkOutAssertLow;

    for (int i = 0; i < 8; ++i)
    {
        const int src = (iecWriteHistoryPos + i) & 7;

        v.writeHistory[i].valid = iecWriteHistory[src].valid;
        v.writeHistory[i].pc = iecWriteHistory[src].pc;
        v.writeHistory[i].retTarget = iecWriteHistory[src].retTarget;
        v.writeHistory[i].address = iecWriteHistory[src].address;
        v.writeHistory[i].reg = iecWriteHistory[src].reg;
        v.writeHistory[i].value = iecWriteHistory[src].value;
        v.writeHistory[i].prAfter = iecWriteHistory[src].prAfter;
        v.writeHistory[i].ddrAfter = iecWriteHistory[src].ddrAfter;
    }

    for (int i = 0; i < 8; ++i)
    {
        const int src = (iecReadHistoryPos + i) & 7;

        v.readHistory[i].valid = iecReadHistory[src].valid;
        v.readHistory[i].address = iecReadHistory[src].address;
        v.readHistory[i].reg = iecReadHistory[src].reg;
        v.readHistory[i].value = iecReadHistory[src].value;
        v.readHistory[i].pc = iecReadHistory[src].pc;
        v.readHistory[i].retTarget = iecReadHistory[src].retTarget;
    }

    v.sameReadCount = sameReadCount;
    v.lastReadValue = lastReadValue;

    return v;
}

D1581CIA::IECOutputDecode D1581CIA::decodeIECOutputs() const
{
    IECOutputDecode d{};

    const uint8_t prb  = getPortBOutputRegister();
    const uint8_t ddrb = getDDRB();

    d.busDirOutput =
        ((ddrb & PRB_BUSDIR) != 0) &&
        ((prb  & PRB_BUSDIR) == 0);

    d.atnAckDataLow =
        ((ddrb & PRB_ATNACK) != 0) &&
        ((prb  & PRB_ATNACK) != 0) &&
        iecAtnInLow;

    d.datOutAssertLow =
        d.busDirOutput &&
       ((ddrb & PRB_DATOUT) != 0) &&
        ((prb  & PRB_DATOUT) != 0);

    d.clkOutAssertLow =
        d.busDirOutput &&
        ((ddrb & PRB_CLKOUT) != 0) &&
        ((prb  & PRB_CLKOUT) != 0);

    d.finalDataLow = d.atnAckDataLow || d.datOutAssertLow;
    d.finalClkLow  = d.clkOutAssertLow;

    return d;
}

void D1581CIA::recordIECWrite(uint16_t pc, uint16_t retTarget, uint16_t address, uint8_t reg, uint8_t value)
{
    IECWriteTrace& e = iecWriteHistory[iecWriteHistoryPos];

    e.valid = true;
    e.pc = pc;
    e.retTarget = retTarget;
    e.address = address;
    e.reg = reg;
    e.value = value;
    e.prAfter = getPortBOutputRegister();
    e.ddrAfter = getDDRB();

    iecWriteHistoryPos = static_cast<uint8_t>((iecWriteHistoryPos + 1) & 15);
}

void D1581CIA::recordDebugCIARead(uint16_t pc, uint16_t retTarget, uint16_t address, uint8_t reg, uint8_t value)
{
    if (value == lastReadValue)
        ++sameReadCount;
    else
    {
        lastReadValue = value;
        sameReadCount = 0;
    }

    IECReadTrace& e = iecReadHistory[iecReadHistoryPos];

    e.valid = true;
    e.pc = pc;
    e.retTarget = retTarget;
    e.address = address;
    e.reg = reg;
    e.value = value;

    iecReadHistoryPos = static_cast<uint8_t>((iecReadHistoryPos + 1) & 7);
}
