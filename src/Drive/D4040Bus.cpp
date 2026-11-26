// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Drive/D4040.h"
#include "Drive/D4040Bus.h"
#include "Drive/D4040Memory.h"

D4040Bus::D4040Bus(Processor processor) :
    drive(nullptr),
    mem(nullptr),
    ieee488Device(nullptr),
    processor(processor)
{

}

D4040Bus::~D4040Bus() = default;

uint8_t D4040Bus::read(uint16_t address)
{
    if (!mem)
        return 0xFF;

    if (processor == Processor::DOS6502)
        updateRIOTInputs();

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

    if (processor == Processor::DOS6502)
        updateIEEE488Outputs();
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
    if (!mem || !drive)
        return;

    if (ieee488Device && ieee488Device->isConnected())
    {
        // Sample the current physical bus state.
        IEEE488Bus::BusState state;

        state.atn = ieee488Device->getBusSignal(IEEE488Bus::Signal::ATN);
        state.dav = ieee488Device->getBusSignal(IEEE488Bus::Signal::DAV);
        state.eoi = ieee488Device->getBusSignal(IEEE488Bus::Signal::EOI);
        state.nrfd = ieee488Device->getBusSignal(IEEE488Bus::Signal::NRFD);
        state.ndac = ieee488Device->getBusSignal(IEEE488Bus::Signal::NDAC);
        state.dio = ieee488Device->getBusDIO();

        updateRIOTInputs(state);
    }
    else
    {
        IEEE488Bus::BusState state;
        updateRIOTInputs(state);
    }
}

void D4040Bus::updateRIOTInputs(const IEEE488Bus::BusState& state)
{
    if (!mem || !drive)
        return;

    MOS6532& riot1 = mem->getRIOT1();
    MOS6532& riot2 = mem->getRIOT2();

    const uint8_t deviceNumber = static_cast<uint8_t>(drive->getDeviceNumber());
    const uint8_t addressBits = static_cast<uint8_t>((deviceNumber - 8) & 0x07);

    uint8_t riot2PA = 0xFF;
    uint8_t riot2PB = static_cast<uint8_t>(0xF8 | addressBits);

    if (ieee488Device && ieee488Device->isConnected())
    {
        riot1.setPortAInput(state.dio);

        // PA7 = inverted physical ATN.
        if (state.atn)
            riot2PA &= static_cast<uint8_t>(~0x80);

        // PA6 = DAV.
        if (!state.dav)
            riot2PA &= static_cast<uint8_t>(~0x40);

        // PA5 = EOI.
        if (!state.eoi)
            riot2PA &= static_cast<uint8_t>(~0x20);

        // PB7 = NRFD.
        if (!state.nrfd)
            riot2PB &= static_cast<uint8_t>(~0x80);

        // PB6 = NDAC.
        if (!state.ndac)
            riot2PB &= static_cast<uint8_t>(~0x40);
    }
    else
    {
        riot1.setPortAInput(0xFF);

        // Preserve the existing disconnected behavior.
        riot2PA &= static_cast<uint8_t>(~0x80);
    }

    riot2.setPortAInput(riot2PA);
    riot2.setPortBInput(riot2PB);
}

void D4040Bus::updateIEEE488Outputs()
{
    if (!mem || !ieee488Device || !ieee488Device->isConnected())
        return;

    const MOS6532& riot1 = mem->getRIOT1();
    const MOS6532& riot2 = mem->getRIOT2();

    // Resolve RIOT output pins.
    // Input-configured pins are treated as high.
    const uint8_t riot1PB = static_cast<uint8_t>(riot1.getPortBOutput() | ~riot1.getDDRB());

    const uint8_t riot2PA = static_cast<uint8_t>(riot2.getPortAOutput() | ~riot2.getDDRA());

    // IEEE-488 ATN is active-low.
    const bool atn = ieee488Device->getBusSignal(IEEE488Bus::Signal::ATN);

    const bool atnActive = !atn;

    // RIOT2 Port A outputs.
    //
    // PA0 = ATNA
    // PA1 = /DACO
    // PA2 = RFDO
    // PA3 = EOIO
    // PA4 = DAVO

    const bool atna   = (riot2PA & 0x01) != 0;
    const bool daco_n = (riot2PA & 0x02) != 0;
    const bool rfdo   = (riot2PA & 0x04) != 0;
    const bool eoio   = (riot2PA & 0x08) != 0;
    const bool davo   = (riot2PA & 0x10) != 0;

    const bool nrfdLevel = (atnActive == atna) && rfdo;
    const bool ndacLevel = !daco_n && (atna || !atnActive);

    // Apply IEEE-488 control outputs.
    ieee488Device->setBusSignal(IEEE488Bus::Signal::NRFD, nrfdLevel);
    ieee488Device->setBusSignal(IEEE488Bus::Signal::NDAC, ndacLevel);
    ieee488Device->setBusSignal(IEEE488Bus::Signal::EOI, eoio);
    ieee488Device->setBusSignal(IEEE488Bus::Signal::DAV, davo);

    const uint8_t data = atnActive ? 0xFF : riot1PB;

    ieee488Device->setBusDIO(data);
}
