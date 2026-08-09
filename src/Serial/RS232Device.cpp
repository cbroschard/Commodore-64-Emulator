// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include <iomanip>
#include "Serial/RS232Device.h"

RS232Device::RS232Device() :
    cycleAccumulator(0),
    rxBitIndex(0),
    rxShift(0),
    lastRXD(true),
    dtr(true),
    dsr(true),
    rts(true),
    txd(true),
    rxd(true),
    cts(true),
    dcd(true),
    ri(true),
    clockHz(1022727.0),
    cyclesPerBit(1022727.0 / 300),
    txState(TxState::Idle),
    txCountdown(0.0),
    txShift(0),
    txOriginalByte(0),
    txBitIndex(0),
    rxCountdown(0.0),
    rxState(RxState::Idle)
{

}

RS232Device::~RS232Device() = default;

void RS232Device::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("RS23");
    wrtr.writeU32(1); // version

    // Configuration
    wrtr.writeU32(config.baud);
    wrtr.writeU8(config.dataBits);
    wrtr.writeU8(config.stopBits);
    wrtr.writeU8(static_cast<uint8_t>(config.parity));

    // Line State
    wrtr.writeBool(dtr);
    wrtr.writeBool(dsr);
    wrtr.writeBool(rts);
    wrtr.writeBool(cts);
    wrtr.writeBool(txd);
    wrtr.writeBool(rxd);
    wrtr.writeBool(dcd);
    wrtr.writeBool(ri);

    // Timing
    wrtr.writeF64(clockHz);
    wrtr.writeF64(cyclesPerBit);
    wrtr.writeU64(cycleAccumulator);

    // TX Engine
    wrtr.writeU8(static_cast<uint8_t>(txState));
    wrtr.writeF64(txCountdown);
    wrtr.writeU8(txShift);
    wrtr.writeU8(txOriginalByte);
    wrtr.writeI32(txBitIndex);

    // RX Engine
    wrtr.writeU8(static_cast<uint8_t>(rxState));
    wrtr.writeF64(rxCountdown);
    wrtr.writeU8(rxShift);
    wrtr.writeI32(rxBitIndex);

    wrtr.writeBool(lastRXD);

    // Queues
    auto writeByteQueue = [&](const std::queue<uint8_t>& queue)
    {
        std::queue<uint8_t> copy = queue;

        wrtr.writeU32(static_cast<uint32_t>(copy.size()));

        while (!copy.empty())
        {
            wrtr.writeU8(copy.front());
            copy.pop();
        }
    };

    writeByteQueue(txBytes);
    writeByteQueue(rxBytes);

    wrtr.endChunk();
}

bool RS232Device::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "RS23", 4) == 0)
    {
        rdr.enterChunkPayload(chunk);

        uint32_t ver = 0;
        if (!rdr.readU32(ver))                                  { rdr.exitChunkPayload(chunk); return false; }
        if (ver != 1)                                           { rdr.exitChunkPayload(chunk); return false; }

        // Configuration
        if (!rdr.readU32(config.baud))                          { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU8(config.dataBits))                       { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU8(config.stopBits))                       { rdr.exitChunkPayload(chunk); return false; }

        uint8_t parityTemp = 0;
        if (!rdr.readU8(parityTemp))                            { rdr.exitChunkPayload(chunk); return false; }
        if (parityTemp > static_cast<uint8_t>(Parity::Even))    { rdr.exitChunkPayload(chunk); return false; }

        config.parity = static_cast<Parity>(parityTemp);

        // Line State
        if (!rdr.readBool(dtr))                                 { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(dsr))                                 { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(rts))                                 { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(cts))                                 { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(txd))                                 { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(rxd))                                 { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(dcd))                                 { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(ri))                                  { rdr.exitChunkPayload(chunk); return false; }

        // Timing
        if (!rdr.readF64(clockHz))                              { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readF64(cyclesPerBit))                         { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU64(cycleAccumulator))                     { rdr.exitChunkPayload(chunk); return false; }

        // TX Engine
        uint8_t txTemp = 0;
        if (!rdr.readU8(txTemp))                                { rdr.exitChunkPayload(chunk); return false; }
        if (txTemp > static_cast<uint8_t>(TxState::StopBit))    { rdr.exitChunkPayload(chunk); return false; }

        txState = static_cast<TxState>(txTemp);

        if (!rdr.readF64(txCountdown))                          { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU8(txShift))                               { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU8(txOriginalByte))                        { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readI32(txBitIndex))                           { rdr.exitChunkPayload(chunk); return false; }

        // Rx Engine
        uint8_t rxTemp = 0;
        if (!rdr.readU8(rxTemp))                                { rdr.exitChunkPayload(chunk); return false; }
        if (rxTemp > static_cast<uint8_t>(RxState::StopBit))    { rdr.exitChunkPayload(chunk); return false; }

        rxState = static_cast<RxState>(rxTemp);

        if (!rdr.readF64(rxCountdown))                          { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU8(rxShift))                               { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readI32(rxBitIndex))                           { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readBool(lastRXD))                             { rdr.exitChunkPayload(chunk); return false; }

        // Queues
        auto readByteQueue = [&](std::queue<uint8_t>& queue) -> bool
        {
            uint32_t count = 0;

            if (!rdr.readU32(count))
                return false;

            std::queue<uint8_t> temp;

            for (uint32_t i = 0; i < count; ++i)
            {
                uint8_t value = 0;

                if (!rdr.readU8(value))
                    return false;

                temp.push(value);
            }

            queue = std::move(temp);
            return true;
        };

        if (!readByteQueue(txBytes))                            { rdr.exitChunkPayload(chunk); return false; }

        if (!readByteQueue(rxBytes))                            { rdr.exitChunkPayload(chunk); return false; }

        rdr.exitChunkPayload(chunk);

        return true;
    }

    return false;
}

void RS232Device::reset()
{
    cycleAccumulator    = 0;

    rxBitIndex          = 0;
    rxShift             = 0;
    lastRXD             = true;

    dtr                 = true;
    dsr                 = true;
    rts                 = true;
    txd                 = true;
    rxd                 = true;
    cts                 = true;
    dcd                 = true;
    ri                  = true;

    txState             = TxState::Idle;
    txCountdown         = 0.0;
    txShift             = 0;
    txOriginalByte      = 0;
    txBitIndex          = 0;

    rxCountdown         = 0.0;
    rxState             = RxState::Idle;

    while (!txBytes.empty())
        txBytes.pop();

    while (!rxBytes.empty())
        rxBytes.pop();

    if (peer)
    {
        peer->rxd = txd;
        peer->dsr = dtr;
        peer->dcd = dtr;
        peer->cts = rts;
    }
}

void RS232Device::tick(uint32_t cyclesElapsed)
{
    if (cyclesElapsed == 0)
        return;

    tickRX(cyclesElapsed);
    tickTX(cyclesElapsed);
}

void RS232Device::setTXD(bool state)
{
    txd = state;

    if (!peer)
        return;

    peer->rxd = state;
}

void RS232Device::setDTR(bool state)
{
    dtr = state;

    if (!peer)
        return;

    // Practical null-modem/modem-present behavior.
    peer->dsr = state;
    peer->dcd = state;
}

void RS232Device::setRTS(bool state)
{
    rts = state;

    if (!peer)
        return;

    peer->cts = state;
}

void RS232Device::setClockRate(double hz)
{
    clockHz = hz;

    if (config.baud == 0)
        config.baud = 300;

    cyclesPerBit = clockHz / static_cast<double>(config.baud);
}

void RS232Device::setConfig(const RS232Config& cfg)
{
    config = cfg;

    if (config.baud == 0)
        config.baud = 300;

    if (config.dataBits == 0 || config.dataBits > 8)
        config.dataBits = 8;

    if (config.stopBits == 0)
        config.stopBits = 1;

    cyclesPerBit = clockHz / static_cast<double>(config.baud);
}

void RS232Device::queueTransmitByte(uint8_t value)
{
    txBytes.push(value);
}

bool RS232Device::isTransmitIdle() const
{
    return txState == TxState::Idle && txBytes.empty();
}

bool RS232Device::hasReceivedByte() const
{
    return !rxBytes.empty();
}

bool RS232Device::popReceivedByte(uint8_t& value)
{
    if (rxBytes.empty())
        return false;

    value = rxBytes.front();
    rxBytes.pop();
    return true;
}

void RS232Device::tickTX(uint32_t cyclesElapsed)
{
    switch (txState)
    {
        case TxState::Idle:
        {
            // Idle serial line is high.
            setTXD(true);

            if (!txBytes.empty())
            {
                txShift = txBytes.front();
                txOriginalByte = txShift;
                txBytes.pop();

                txBitIndex = 0;
                txCountdown = cyclesPerBit;

                // Start bit is low.
                setTXD(false);
                txState = TxState::StartBit;
            }
            break;
        }

        case TxState::StartBit:
        {
            txCountdown -= static_cast<double>(cyclesElapsed);

            if (txCountdown <= 0.0)
            {
                const bool bit = (txShift & 0x01) != 0;
                setTXD(bit);

                txShift >>= 1;
                txBitIndex = 1;
                txCountdown += cyclesPerBit;

                txState = TxState::DataBits;
            }
            break;
        }

        case TxState::DataBits:
        {
            txCountdown -= static_cast<double>(cyclesElapsed);

            while (txCountdown <= 0.0 && txState == TxState::DataBits)
            {
                if (txBitIndex >= config.dataBits)
                {
                    if (config.parity != Parity::None)
                    {
                        const bool parityBit = calculateParity(txOriginalByte);

                        setTXD(parityBit);

                        txCountdown += cyclesPerBit;
                        txState = TxState::ParityBit;
                    }
                    else
                    {
                        setTXD(true);

                        txCountdown += cyclesPerBit * config.stopBits;
                        txState = TxState::StopBit;
                    }

                    break;
                }

                const bool bit = (txShift & 0x01) != 0;
                setTXD(bit);

                txShift >>= 1;
                ++txBitIndex;
                txCountdown += cyclesPerBit;
            }
            break;
        }

        case TxState::ParityBit:
        {
            txCountdown -= static_cast<double>(cyclesElapsed);

            if (txCountdown <= 0.0)
            {
                setTXD(true);

                txCountdown += cyclesPerBit * config.stopBits;
                txState = TxState::StopBit;
            }

            break;
        }

        case TxState::StopBit:
        {
            txCountdown -= static_cast<double>(cyclesElapsed);

            if (txCountdown <= 0.0)
            {
                setTXD(true);
                txState = TxState::Idle;
                txCountdown = 0.0;
                txBitIndex = 0;
                txShift = 0;
                txOriginalByte = 0;
            }

            break;
        }
    }
}

void RS232Device::tickRX(uint32_t cyclesElapsed)
{
    switch (rxState)
    {
        case RxState::Idle:
        {
            // Start bit = RXD falling from high to low.
            if (lastRXD && !rxd)
            {
                rxState = RxState::DataBits;
                rxCountdown = cyclesPerBit * 1.5;
                rxShift = 0;
                rxBitIndex = 0;
            }
            break;
        }

        case RxState::DataBits:
        {
            rxCountdown -= static_cast<double>(cyclesElapsed);

            while (rxCountdown <= 0.0 && rxState == RxState::DataBits)
            {
                if (rxd)
                    rxShift |= static_cast<uint8_t>(1u << rxBitIndex);

                ++rxBitIndex;

                if (rxBitIndex >= config.dataBits)
                {
                    if (config.parity != Parity::None)
                    {
                        rxState = RxState::ParityBit;
                        rxCountdown += cyclesPerBit;
                    }
                    else
                    {
                        rxState = RxState::StopBit;
                        rxCountdown += cyclesPerBit;
                    }

                    break;
                }
                rxCountdown += cyclesPerBit;
            }
            break;
        }

        case RxState::ParityBit:
        {
            rxCountdown -= static_cast<double>(cyclesElapsed);

            if (rxCountdown <= 0.0)
            {
                const bool expectedParity = calculateParity(rxShift);
                const bool receivedParity = rxd;

                if (receivedParity != expectedParity)
                {
                    rxState = RxState::Idle;
                    rxCountdown = 0.0;
                    rxBitIndex = 0;
                    rxShift = 0;
                    break;
                }

                rxState = RxState::StopBit;
                rxCountdown += cyclesPerBit;
            }

            break;
        }

        case RxState::StopBit:
        {
            rxCountdown -= static_cast<double>(cyclesElapsed);

            if (rxCountdown <= 0.0)
            {
                if (rxd)
                    rxBytes.push(rxShift);

                rxState = RxState::Idle;
                rxCountdown = 0.0;
                rxBitIndex = 0;
                rxShift = 0;
            }
            break;
        }
    }

    lastRXD = rxd;
}

bool RS232Device::calculateParity(uint8_t value) const
{
    bool parity = false;

    for (uint8_t bit = 0; bit < config.dataBits; ++bit)
    {
        if (value & static_cast<uint8_t>(1u << bit))
            parity = !parity;
    }

    if (config.parity == Parity::Even)
        return parity;

    if (config.parity == Parity::Odd)
        return !parity;

    return false;
}

std::string RS232Device::debugString() const
{
    std::ostringstream out;

    out << "RS232 Device:\n";
    out << "  Outputs: "
        << "TXD=" << (txd ? "H" : "L") << " "
        << "DTR=" << (dtr ? "H" : "L") << " "
        << "RTS=" << (rts ? "H" : "L") << "\n";

    out << "  Inputs:  "
        << "RXD=" << (rxd ? "H" : "L") << " "
        << "DSR=" << (dsr ? "H" : "L") << " "
        << "CTS=" << (cts ? "H" : "L") << " "
        << "DCD=" << (dcd ? "H" : "L") << " "
        << "RI="  << (ri  ? "H" : "L") << "\n";

    out << "  Peer: " << (peer ? "attached" : "none") << "\n";

    out << "  TX Engine: "
    << "state=";

    switch (txState)
    {
        case TxState::Idle:     out << "Idle"; break;
        case TxState::StartBit: out << "StartBit"; break;
        case TxState::DataBits: out << "DataBits"; break;
        case TxState::ParityBit: out << "ParityBit"; break;
        case TxState::StopBit:  out << "StopBit"; break;
    }

    out << " bit=" << int(txBitIndex)
        << " shift=$" << std::hex << std::uppercase << int(txShift)
        << std::dec
        << " queued=" << txBytes.size()
        << "\n";

    out << "  RX Engine: "
    << "state=";

    switch (rxState)
    {
        case RxState::Idle:     out << "Idle"; break;
        case RxState::DataBits: out << "DataBits"; break;
        case RxState::ParityBit: out << "ParityBit"; break;
        case RxState::StopBit:  out << "StopBit"; break;
    }

    out << " bit=" << int(rxBitIndex)
        << " shift=$" << std::hex << std::uppercase << int(rxShift)
        << std::dec
        << " queued=" << rxBytes.size()
        << " baud=" << config.baud
        << "\n";

    return out.str();
}

std::string RS232Device::selfTest(uint8_t testByte, Parity parity)
{
    std::ostringstream out;

    RS232Device testPeer;

    RS232Config testConfig = config;
    testConfig.parity = parity;

    setConfig(testConfig);

    testPeer.setClockRate(clockHz);
    testPeer.setConfig(testConfig);

    // Preserve the existing peer.
    RS232Device* oldPeer = peer;

    // Temporarily connect both devices.
    attachPeerDevice(&testPeer);
    testPeer.attachPeerDevice(this);

    // Start from deterministic serial state.
    reset();
    testPeer.reset();

    // Queue test byte.
    queueTransmitByte(testByte);

    const uint32_t parityBits = (config.parity == Parity::None) ? 0u : 1u;

    const uint32_t frameBits = 1u + static_cast<uint32_t>(config.dataBits) + parityBits + static_cast<uint32_t>(config.stopBits);
    const uint32_t maxCycles = static_cast<uint32_t>(cyclesPerBit * frameBits * 2.0);

    uint8_t receivedByte = 0;
    bool received = false;

    for (uint32_t cycle = 0; cycle < maxCycles; ++cycle)
    {
        tick(1);
        testPeer.tick(1);

        if (testPeer.popReceivedByte(receivedByte))
        {
            received = true;
            break;
        }
    }

    // Disconnect temporary peer.
    attachPeerDevice(oldPeer);
    testPeer.attachPeerDevice(nullptr);

    out << "RS232 Loopback Test\n";
    out << "-------------------\n";

    out << "Sent:     $"
        << std::hex << std::uppercase
        << std::setw(2) << std::setfill('0')
        << static_cast<int>(testByte)
        << "\n";

    if (received)
    {
        out << "Received: $"
            << std::setw(2)
            << static_cast<int>(receivedByte)
            << "\n";

        out << "Result:   "
            << (receivedByte == testByte ? "PASS" : "FAIL")
            << "\n";
    }
    else
    {
        out << "Received: none\n";
        out << "Result:   FAIL (timeout)\n";
    }

    out << std::dec << std::setfill(' ');

    return out.str();
}
