// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Serial/MOS6551.h"
#include "Serial/RS232Device.h"

MOS6551::MOS6551(RS232Device& serial) :
    serial(serial)
{
    reset();
}

MOS6551::~MOS6551() = default;

void MOS6551::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("6551");
    wrtr.writeU32(1); // Version

    wrtr.writeU8(receiveData);
    wrtr.writeU8(transmitData);

    wrtr.writeU8(statusRegister);
    wrtr.writeU8(commandRegister);
    wrtr.writeU8(controlRegister);

    wrtr.writeBool(irq);

    wrtr.writeBool(rxd);
    wrtr.writeBool(txd);

    wrtr.writeBool(rts);
    wrtr.writeBool(cts);
    wrtr.writeBool(dtr);
    wrtr.writeBool(dsr);
    wrtr.writeBool(dcd);

    wrtr.writeBool(lastDCD);
    wrtr.writeBool(lastDSR);

    wrtr.writeBool(latchedDCD);
    wrtr.writeBool(latchedDSR);
    wrtr.writeBool(modemStatusLatched);

    wrtr.endChunk();
}

bool MOS6551::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "6551", 4) == 0)
    {
        rdr.enterChunkPayload(chunk);

        uint32_t ver = 0;
        if (!rdr.readU32(ver))                  { rdr.exitChunkPayload(chunk); return false; }
        if (ver != 1)                           { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readU8(receiveData))           { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU8(transmitData))          { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readU8(statusRegister))        { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU8(commandRegister))       { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU8(controlRegister))       { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readBool(irq))                 { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readBool(rxd))                 { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(txd))                 { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readBool(rts))                 { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(cts))                 { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(dtr))                 { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(dsr))                 { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(dcd))                 { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readBool(lastDCD))             { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(lastDSR))             { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readBool(latchedDCD))          { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(latchedDSR))          { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(modemStatusLatched))  { rdr.exitChunkPayload(chunk); return false; }

        rdr.exitChunkPayload(chunk);

        // Normalize
        updateCommand();
        updateControl();
        updateIRQ();
        updateStatus();

        return true;
    }

    return false;
}

void MOS6551::reset()
{
    receiveData         = 0x00;
    transmitData        = 0x00;

    statusRegister      = STATUS_TDRE;
    commandRegister     = 0x00;
    controlRegister     = 0x00;

    irq                 = false;

    rxd                 = true;
    txd                 = true;

    rts                 = true;
    cts                 = false;
    dtr                 = true;
    dcd                 = false;
    dsr                 = false;

    lastDCD             = false;
    lastDSR             = false;

    latchedDCD          = dcd;
    latchedDSR          = dsr;
    modemStatusLatched  = false;

    updateStatus();
}

void MOS6551::tick(uint32_t cycles)
{
    serial.tick(cycles);

    // ---------------------------------------------------------
    // Modem input lines
    // ---------------------------------------------------------

    cts = serial.getCTS();

    const bool newDCD = serial.getDCD();
    const bool newDSR = serial.getDSR();

    const bool dcdChanged = newDCD != lastDCD;
    const bool dsrChanged = newDSR != lastDSR;

    if (!modemStatusLatched && (dcdChanged || dsrChanged))
    {
        latchedDCD = newDCD;
        latchedDSR = newDSR;

        modemStatusLatched = true;

        // IRD disables receiver-related IRQs including
        // DCD/DSR change interrupts.
        if ((commandRegister & CMD_IRD) == 0)
            irq = true;
    }

    // Always keep track of the actual live pin levels.
    dcd = newDCD;
    dsr = newDSR;

    lastDCD = newDCD;
    lastDSR = newDSR;

    if (serial.canAcceptTransmitByte())
        statusRegister |= STATUS_TDRE;
    else
        statusRegister &= static_cast<uint8_t>(~STATUS_TDRE);

    uint8_t value = 0;

    while (serial.popReceivedByte(value))
    {
        if (statusRegister & STATUS_RDRF)
            statusRegister |= STATUS_OVRN;

        receiveData = value;
        statusRegister |= STATUS_RDRF;

        if (serial.hasParityError())
            statusRegister |= STATUS_PE;

        if (serial.hasFramingError())
            statusRegister |= STATUS_FE;
    }

    updateIRQ();
    updateStatus();
}

uint8_t MOS6551::read(uint16_t reg)
{
    switch (reg & 0x03)
    {
        case 0x00:
        {
            const uint8_t value = receiveData;

            statusRegister &= static_cast<uint8_t>(~(STATUS_RDRF |STATUS_OVRN | STATUS_FE | STATUS_PE)); serial.clearReceiveErrors();

            updateIRQ();
            updateStatus();

            return value;
        }
        case 0x01:
        {
            updateStatus();

            const uint8_t value = statusRegister;

            // Reading Status acknowledges the current IRQ.
            irq = false;

            if (modemStatusLatched)
            {
                modemStatusLatched = false;

                // If the physical modem lines changed again while
                // the old state was latched, capture the new state.
                if (dcd != latchedDCD || dsr != latchedDSR)
                {
                    latchedDCD = dcd;
                    latchedDSR = dsr;
                    modemStatusLatched = true;

                    if ((commandRegister & CMD_IRD) == 0)
                        irq = true;
                }
            }

            updateStatus();

            return value;
        }
        case 0x02:
            return commandRegister;

        case 0x03:
            return controlRegister;
    }

    return 0xFF;
}

void MOS6551::write(uint16_t reg, uint8_t value)
{
    switch (reg & 0x03)
    {
        case 0x00:
        {
             const uint8_t tic = commandRegister & CMD_TIC_MASK;

            // Transmitter disabled or BREAK mode.
            if (tic == 0x00 || tic == 0x0C)
                break;

            if (!serial.canAcceptTransmitByte())
                break;

            transmitData = value;
            statusRegister &= static_cast<uint8_t>(~STATUS_TDRE);

            serial.queueTransmitByte(value);

            updateIRQ();
            break;
        }

        case 0x01:
            programmedReset();
            break;

        case 0x02:
            commandRegister = value;
            updateCommand();
            updateIRQ();
            break;

        case 0x03:
            controlRegister = value;
            updateControl();
            break;
    }
}

void MOS6551::updateStatus()
{
    statusRegister &= static_cast<uint8_t>(STATUS_TDRE | STATUS_RDRF | STATUS_OVRN | STATUS_FE | STATUS_PE);

    const bool statusDSR = modemStatusLatched ? latchedDSR : dsr;
    const bool statusDCD = modemStatusLatched ? latchedDCD : dcd;

    if (statusDSR)
        statusRegister |= STATUS_DSR;

    if (statusDCD)
        statusRegister |= STATUS_DCD;

    if (irq)
        statusRegister |= STATUS_IRQ;
}

void MOS6551::updateCommand()
{
    RS232Device::RS232Config config = serial.getConfig();

    // Parity
    if ((commandRegister & CMD_PME) == 0)
        config.parity = RS232Device::Parity::None;
    else
    {
        switch ((commandRegister & CMD_PMC_MASK) >> 6)
        {
            case 0x00: config.parity = RS232Device::Parity::Odd;   break;
            case 0x01: config.parity = RS232Device::Parity::Even;  break;
            case 0x02: config.parity = RS232Device::Parity::Mark;  break;
            case 0x03: config.parity = RS232Device::Parity::Space; break;
        }
    }

    serial.setConfig(config);

    // DTR
    dtr = (commandRegister & CMD_DTR) == 0;
    serial.setDTR(dtr);

    // RTS / transmitter control
    const uint8_t tic = commandRegister & CMD_TIC_MASK;

    switch (tic)
    {
        case 0x00:
            rts = true;
            serial.setBreak(false);
            break;

        case 0x04:
        case 0x08:
            rts = false;
            serial.setBreak(false);
            break;

        case 0x0C:
            rts = false;
            serial.setBreak(true);
            break;
    }

    serial.setRTS(rts);
}

void MOS6551::updateIRQ()
{
    const bool dtrEnabled = (commandRegister & CMD_DTR) != 0;
    const bool rxIRQEnabled = dtrEnabled && ((commandRegister & CMD_IRD) == 0);

    if (rxIRQEnabled && (statusRegister & STATUS_RDRF))
        irq = true;

    const uint8_t tic = commandRegister & CMD_TIC_MASK;

    if (dtrEnabled && tic == 0x04 && (statusRegister & STATUS_TDRE))
        irq = true;

    updateStatus();
}

void MOS6551::updateControl()
{
    RS232Device::RS232Config config = serial.getConfig();

    const uint32_t baud = decodeBaudRate();

    if (baud != 0)
        config.baud = baud;

    config.dataBits = decodeWordLength();
    config.stopBits = decodeStopBits();

    serial.setConfig(config);
}

void MOS6551::programmedReset()
{
    // Programmed reset clears Command Register bits 0-4.
    commandRegister &= 0xE0;

    // Clear overrun only.
    statusRegister &= static_cast<uint8_t>(~STATUS_OVRN);

    // IRQ indication is acknowledged/reset.
    irq = false;

    updateCommand();
    updateIRQ();
    updateStatus();
}

uint32_t MOS6551::decodeBaudRate() const
{
    switch (controlRegister & CTRL_SBR_MASK)
    {
        case 0x01: return 50;
        case 0x02: return 75;
        case 0x03: return 110;
        case 0x04: return 135;
        case 0x05: return 150;
        case 0x06: return 300;
        case 0x07: return 600;
        case 0x08: return 1200;
        case 0x09: return 1800;
        case 0x0A: return 2400;
        case 0x0B: return 3600;
        case 0x0C: return 4800;
        case 0x0D: return 7200;
        case 0x0E: return 9600;
        case 0x0F: return 19200;

        case 0x00:
        default:
            return 0; // External clock
    }
}

uint8_t MOS6551::decodeWordLength() const
{
    switch ((controlRegister & CTRL_WL_MASK) >> 5)
    {
        case 0x00: return 8;
        case 0x01: return 7;
        case 0x02: return 6;
        case 0x03: return 5;
    }

    return 8;
}

double MOS6551::decodeStopBits() const
{
    if ((controlRegister & CTRL_SBN) == 0)
        return 1.0;

    const uint8_t wordLength = decodeWordLength();
    const bool parityEnabled = (commandRegister & CMD_PME) != 0;

    if (wordLength == 5 && !parityEnabled)
        return 1.5;

    if (wordLength == 8 && parityEnabled)
        return 1.0;

    return 2.0;
}
