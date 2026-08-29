// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Serial/MOS6551.h"
#include "Serial/RS232Device.h"
#include "Serial/RS232Endpoint.h"

MOS6551::MOS6551(RS232Device& serial) :
    serial(serial),
    endpoint(nullptr),
    baudMultiplier(1.0)
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

    wrtr.writeBool(echoPending);
    wrtr.writeBool(echoLevel);
    wrtr.writeF64(echoCountdown);

    wrtr.writeF64(baudMultiplier);

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

        if (!rdr.readBool(echoPending))         { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(echoLevel))           { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readF64(echoCountdown))        { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readF64(baudMultiplier))       { rdr.exitChunkPayload(chunk); return false; }

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
    dtr                 = true;
    cts                 = serial.getCTS();
    dcd                 = serial.getDCD();
    dsr                 = serial.getDSR();

    lastDCD             = dcd;
    lastDSR             = dsr;

    latchedDCD          = dcd;
    latchedDSR          = dsr;

    modemStatusLatched  = false;

    echoPending         = false;
    echoLevel           = true;
    echoCountdown       = 0.0;

    txBusy              = false;
    txCountdown         = 0.0;

    rxBusy              = false;
    rxCountdown         = 0.0;
    rxPendingByte       = 0;

    updateStatus();
}

void MOS6551::tick(uint32_t cycles)
{
    serial.tick(cycles);

    if (endpoint)
    {
        endpoint->tick();

        if (!rxBusy)
        {
            uint8_t value = 0;

            if (endpoint->readByte(value))
            {
                rxPendingByte = value;
                rxCountdown = characterCycles();
                rxBusy = true;
            }
        }
    }

    if (rxBusy)
    {
        rxCountdown -= static_cast<double>(cycles);

        if (rxCountdown <= 0.0)
        {
            receiveByte(rxPendingByte);

            rxBusy = false;
            rxCountdown = 0.0;
        }
    }

    if (txBusy)
    {
        txCountdown -= static_cast<double>(cycles);

        if (txCountdown <= 0.0)
        {
            if (endpoint)
                endpoint->writeByte(transmitData);
            else
                serial.queueTransmitByte(transmitData);

            txBusy = false;
            txCountdown = 0.0;

            statusRegister |= STATUS_TDRE;
        }
    }

    const bool newRXD = serial.getRXD();
    const bool echoEnabled = (commandRegister & CMD_REM) != 0;

    if (echoEnabled)
    {
        if (newRXD != rxd)
        {
            echoLevel = newRXD;
            echoPending = true;
            echoCountdown =
                serial.getCyclesPerBit() * 0.5;
        }

        if (echoPending)
        {
            echoCountdown -= static_cast<double>(cycles);

            if (echoCountdown <= 0.0)
            {
                serial.setTXD(echoLevel);
                txd = echoLevel;

                echoPending = false;
                echoCountdown = 0.0;
            }
        }
    }
    else
    {
        echoPending = false;
        echoCountdown = 0.0;
    }

    rxd = newRXD;

    cts = serial.getCTS();

    const bool newDCD = serial.getDCD();
    const bool newDSR = serial.getDSR();

    const bool dcdChanged = newDCD != lastDCD;
    const bool dsrChanged = newDSR != lastDSR;

    const bool dtrEnabled = (commandRegister & CMD_DTR) != 0;

    const bool receiverIRQEnabled = dtrEnabled && ((commandRegister & CMD_IRD) == 0);

    if (!modemStatusLatched && (dcdChanged || dsrChanged))
    {
        latchedDCD = newDCD;
        latchedDSR = newDSR;

        modemStatusLatched = true;

        if (receiverIRQEnabled)
            irq = true;
    }

    dcd = newDCD;
    dsr = newDSR;

    lastDCD = newDCD;
    lastDSR = newDSR;

    if (!endpoint)
    {
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

            statusRegister &= static_cast<uint8_t>(~(STATUS_RDRF |STATUS_OVRN | STATUS_FE | STATUS_PE));

            serial.clearReceiveErrors();

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

uint8_t MOS6551::peek(uint16_t reg) const
{
    reg &= 0x03;

    switch (reg)
    {
        case 0x00:
            // RX data register.
            // Return current received data WITHOUT clearing
            // receive-ready / IRQ state.
            return receiveData;

        case 0x01:
            return getStatusRegister();

        case 0x02:
            return getCommandRegister();

        case 0x03:
            return getControlRegister();

        default:
            return 0xFF;
    }
}

void MOS6551::write(uint16_t reg, uint8_t value)
{
    switch (reg & 0x03)
    {
        case 0x00:
        {
            const bool echoEnabled = (commandRegister & CMD_REM) != 0;

            const uint8_t tic = commandRegister & CMD_TIC_MASK;

            if (echoEnabled || tic == 0x00 || tic == 0x0C)
                break;

            if (txBusy)
                break;

            transmitData = value;

            statusRegister &= static_cast<uint8_t>(~STATUS_TDRE);

            txBusy = true;
            txCountdown = characterCycles();

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

void MOS6551::setBaudMultiplier(double multiplier)
{
    baudMultiplier = multiplier;
    updateControl();
}

void MOS6551::receiveByte(uint8_t value)
{
    if (statusRegister & STATUS_RDRF)
        statusRegister |= STATUS_OVRN;

    receiveData = value;
    statusRegister |= STATUS_RDRF;

    const bool dtrEnabled = (commandRegister & CMD_DTR) != 0;

    const bool receiverIRQEnabled = dtrEnabled && ((commandRegister & CMD_IRD) == 0);

    if (receiverIRQEnabled)
        irq = true;

    updateStatus();
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

    const bool echoEnabled = (commandRegister & CMD_REM) != 0;

    // RTS / transmitter control
    const uint8_t tic = commandRegister & CMD_TIC_MASK;

    if (echoEnabled)
    {
        // Receiver echo mode requires TIC = 00.
        // RTSB is low in receiver echo mode.
        rts = false;

        serial.setBreak(false);
    }
    else
    {
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
    }

    serial.setRTS(rts);
}

void MOS6551::updateIRQ()
{
    const bool dtrEnabled = (commandRegister & CMD_DTR) != 0;

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
        config.baud = static_cast<uint32_t>(static_cast<double>(baud) * baudMultiplier);

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

double MOS6551::characterCycles() const
{
    const auto& config = serial.getConfig();

    double bits = 1.0; // start bit
    bits += static_cast<double>(config.dataBits);

    if (config.parity != RS232Device::Parity::None)
        bits += 1.0;

    bits += config.stopBits;

    return serial.getCyclesPerBit() * bits;
}
