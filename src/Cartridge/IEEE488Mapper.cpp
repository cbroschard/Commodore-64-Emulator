// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "Cartridge/IEEE488Mapper.h"

IEEE488Mapper::IEEE488Mapper() :
    ieee488Bus(nullptr)
{

}

IEEE488Mapper::~IEEE488Mapper()
{
    if (ieee488Bus)
    {
        ieee488Bus->detachDevice(CONTROLLER_ID);
        ieee488Bus = nullptr;
    }
}

bool IEEE488Mapper::attachIEEE488BusInstance(IEEE488Bus* newBus)
{
    if (ieee488Bus == newBus)
        return true;

    if (ieee488Bus)
        ieee488Bus->detachDevice(CONTROLLER_ID);

    ieee488Bus = nullptr;

    tpi.setPortAInput(0xFF);
    tpi.setPortBInput(0xFF);

    if (!newBus)
        return true;

    if (!newBus->attachController(CONTROLLER_ID))
        return false;

    ieee488Bus = newBus;

    updateATN();
    updateDAV();
    updateEOI();
    updateNRFD();
    updateNDAC();
    updateDataLines(isTransmitting());
    sampleControlLines();
    sampleDataLines();

    return true;
}

void IEEE488Mapper::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("I488");
    wrtr.writeU32(1); // Version

    // Dump TPI6525
    tpi.save(wrtr);

    wrtr.endChunk();
}

bool IEEE488Mapper::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "I488", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    uint32_t ver = 0;
    if (!rdr.readU32(ver))      { rdr.exitChunkPayload(chunk); return false; }
    if (ver != 1)               { rdr.exitChunkPayload(chunk); return false; }

    if (!tpi.load(rdr))         { rdr.exitChunkPayload(chunk); return false; }

    rdr.exitChunkPayload(chunk);

    // Resynchronize external cartridge outputs after restoring TPI state.
    updateLines();
    updateATN();
    updateDAV();
    updateEOI();
    updateNRFD();
    updateNDAC();
    updateDataLines(isTransmitting());

    return true;
}

void IEEE488Mapper::reset()
{
    tpi.reset();

    updateLines();

    updateATN();
    updateDAV();
    updateEOI();
    updateNRFD();
    updateNDAC();
    updateDataLines(isTransmitting());

    sampleControlLines();
    sampleDataLines();
}

void IEEE488Mapper::tick(uint32_t elapsedCycles)
{
    tpi.tick(elapsedCycles);
    sampleControlLines();
    sampleDataLines();
}

uint8_t IEEE488Mapper::read(uint16_t address)
{
    if (!cart)
        return 0xFF;

   if (address >= 0x8000 && address <= 0x9FFF)
    {
        if (tpi.getPortCPins() & 0x10)
            return cart->readCartridge(static_cast<uint16_t>(address & 0x0FFF), cartLocation::LO);
    }

    if (address >= 0xDF00 && address <= 0xDFFF)
    {
        const uint8_t reg = static_cast<uint8_t>(address & 0x07);

        if (reg == 0)
            sampleControlLines();

        if (reg == 1)
            sampleDataLines();

        return tpi.readRegister(reg);
    }

    return cart->sampleDataBus();
}

void IEEE488Mapper::write(uint16_t address, uint8_t value)
{
    if (address < 0xDF00 || address > 0xDFFF)
        return;

    const uint8_t reg = static_cast<uint8_t>(address & 0x07);

    tpi.writeRegister(reg, value);

    // Synchronize cartridge mapping (EXROM / GAME).
    updateLines();

    // Port A or DDRA changed.
    if (reg == 0x00 || reg == 0x03)
    {
        // Update IEEE-488 handshake outputs.
        updateATN();
        updateDAV();
        updateEOI();
        updateNRFD();
        updateNDAC();

        // Update DIO direction and output state.
        updateDataLines(isTransmitting());

        // Sample the newly resolved bus state.
        sampleControlLines();
        sampleDataLines();
    }

    // Port B or DDRB changed.
    else if (reg == 0x01 || reg == 0x04)
    {
        // Update DIO outputs.
        updateDataLines(isTransmitting());

        // Refresh external DIO inputs.
        sampleDataLines();
    }
}

uint8_t IEEE488Mapper::peek(uint16_t address) const
{
    if (!cart)
        return 0xFF;

    if (address >= 0x8000 && address <= 0x9FFF)
    {
        if (tpi.getPortCPins() & 0x10)
            return cart->readCartridge(static_cast<uint16_t>(address & 0x0FFF), cartLocation::LO);
    }

    if (address >= 0xDF00 && address <= 0xDFFF)
    {
        const uint8_t reg = static_cast<uint8_t>(address & 0x07);
        return tpi.peek(reg);
    }

    return cart->sampleDataBus();
}

bool IEEE488Mapper::loadIntoMemory(uint8_t bank)
{
    if (!cart)
        return false;

    cart->clearCartridge(cartLocation::LO);
    cart->clearCartridge(cartLocation::HI);
    cart->clearCartridge(cartLocation::HI_E000);

    for (const auto& section : cart->getChipSections())
    {
        if (section.bankNumber != bank || section.loadAddress != 0x8000 || section.data.size() != 4096)
            continue;

        for (size_t i = 0; i < section.data.size(); ++i)
            cart->writeCartridge(static_cast<uint16_t>(i), section.data[i], cartLocation::LO);

        return true;
    }

    return false;
}

bool IEEE488Mapper::applyMappingAfterLoad()
{
    if (!cart)
        return false;

    if (!loadIntoMemory(0))
        return false;

    updateLines();

    return true;
}

bool IEEE488Mapper::readDrivesBus(uint16_t address) const
{
    if (address >= 0x8000 && address <= 0x9FFF)
        return (tpi.getPortCPins() & 0x10) != 0;

    return address >= 0xDF00 && address <= 0xDFFF;
}

bool IEEE488Mapper::isRegionEnabled(CartRegion region) const
{
    if (region == CartRegion::ROM_8000_9FFF)
        return (tpi.getPortCPins() & 0x10) != 0;

    return true;
}

void IEEE488Mapper::updateLines()
{
    if (!cart)
        return;

    const uint8_t portC = tpi.getPortCPins();
    const bool pc3High = (portC & 0x08) != 0;

    cart->setGameLine(true);
    cart->setExROMLine(!pc3High);
}

void IEEE488Mapper::updateDataLines(bool transmitting)
{
    if (!ieee488Bus)
        return;

    if (transmitting)
        ieee488Bus->setDIO(CONTROLLER_ID, tpi.getPortBDriveValue());
    else
        ieee488Bus->setDIO(CONTROLLER_ID, 0xFF);
}

void IEEE488Mapper::sampleDataLines()
{
    if (!ieee488Bus)
        return;

    // During transmission, external Port B inputs are released.
    if (isTransmitting())
    {
        tpi.setPortBInput(0xFF);
        return;
    }

    // During reception, sample the resolved IEEE-488 data bus.
    tpi.setPortBInput(ieee488Bus->getDIO());
}

void IEEE488Mapper::sampleControlLines()
{
    if (!ieee488Bus)
        return;

    const uint8_t portA = tpi.getPortAPins();

    const bool transmitting = (portA & 0x02) != 0;
    const bool deviceMode   = (portA & 0x01) != 0;

    uint8_t input = tpi.getPortAInput();

    // Release the externally sampled control inputs.
    // Preserve PA0-PA2.
    input |= 0xF8;

    if (transmitting)
    {
        // Transmitting: observe the listener handshake.
        if (!ieee488Bus->getSignal(IEEE488Bus::Signal::NRFD))
            input &= static_cast<uint8_t>(~0x80);

        if (!ieee488Bus->getSignal(IEEE488Bus::Signal::NDAC))
            input &= static_cast<uint8_t>(~0x40);
    }
    else
    {
        // Receiving: observe the talker's handshake.
        if (!ieee488Bus->getSignal(IEEE488Bus::Signal::DAV))
            input &= static_cast<uint8_t>(~0x10);

        if (!ieee488Bus->getSignal(IEEE488Bus::Signal::EOI))
            input &= static_cast<uint8_t>(~0x20);
    }

    // In device mode, ATN becomes an input.
    if (deviceMode)
    {
        if (!ieee488Bus->getSignal(IEEE488Bus::Signal::ATN))
            input &= static_cast<uint8_t>(~0x08);
    }

    tpi.setPortAInput(input);
}

void IEEE488Mapper::updateATN()
{
    if (!ieee488Bus)
        return;

    const uint8_t portA = tpi.getPortAPins();

    // Device mode: release ATN
    if (portA & 0x01)
    {
        ieee488Bus->setSignal(CONTROLLER_ID, IEEE488Bus::Signal::ATN, true);
        return;
    }

    // Controller mode: PA3 controls ATN
    ieee488Bus->setSignal(CONTROLLER_ID, IEEE488Bus::Signal::ATN, (portA & 0x08) != 0);
}

void IEEE488Mapper::updateDAV()
{
    if (!ieee488Bus)
        return;

    bool dav = true;

    if (isTransmitting())
        dav = (tpi.getPortAPins() & 0x10) != 0;

    ieee488Bus->setSignal(CONTROLLER_ID, IEEE488Bus::Signal::DAV, dav);
}

void IEEE488Mapper::updateEOI()
{
    if (!ieee488Bus)
        return;

    bool eoi = true;

    if (isTransmitting())
        eoi = (tpi.getPortAPins() & 0x20) != 0;

    ieee488Bus->setSignal(CONTROLLER_ID, IEEE488Bus::Signal::EOI, eoi);
}

void IEEE488Mapper::updateNRFD()
{
    if (!ieee488Bus)
        return;

    bool nrfd = true;

    if (!isTransmitting())
        nrfd = (tpi.getPortAPins() & 0x80) != 0;

    ieee488Bus->setSignal(CONTROLLER_ID, IEEE488Bus::Signal::NRFD, nrfd);
}

void IEEE488Mapper::updateNDAC()
{
    if (!ieee488Bus)
        return;

    bool ndac = true;

    if (!isTransmitting())
        ndac = (tpi.getPortAPins() & 0x40) != 0;

    ieee488Bus->setSignal(CONTROLLER_ID, IEEE488Bus::Signal::NDAC, ndac);
}
