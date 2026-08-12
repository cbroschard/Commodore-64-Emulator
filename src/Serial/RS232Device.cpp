// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include <iomanip>
#include "Serial/RS232Device.h"
#include "Serial/RS232Endpoint.h"

RS232Device::RS232Device() :
    peer(nullptr),
    endpoint(nullptr),
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
    rxStartPending(false),
    parityError(false),
    framingError(false),
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
    wrtr.writeU32(3); // version

    // Configuration
    wrtr.writeU32(config.baud);
    wrtr.writeU8(config.dataBits);
    wrtr.writeU8(config.stopBits);
    wrtr.writeU8(static_cast<uint8_t>(config.parity));
    wrtr.writeU8(static_cast<uint8_t>(config.flowControl));

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
    wrtr.writeBool(parityError);
    wrtr.writeBool(framingError);

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
        if (!rdr.readU32(ver))                                      { rdr.exitChunkPayload(chunk); return false; }
        if (ver != 3)                                               { rdr.exitChunkPayload(chunk); return false; }

        // Configuration
        if (!rdr.readU32(config.baud))                              { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU8(config.dataBits))                           { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU8(config.stopBits))                           { rdr.exitChunkPayload(chunk); return false; }

        uint8_t parityTemp = 0;
        if (!rdr.readU8(parityTemp))                                { rdr.exitChunkPayload(chunk); return false; }
        if (parityTemp > static_cast<uint8_t>(Parity::Space))       { rdr.exitChunkPayload(chunk); return false; }

        config.parity = static_cast<Parity>(parityTemp);

        uint8_t flowTemp = 0;

        if (!rdr.readU8(flowTemp))                                  { rdr.exitChunkPayload(chunk); return false; }
        if (flowTemp > static_cast<uint8_t>(FlowControl::RTS_CTS))  { rdr.exitChunkPayload(chunk); return false; }

        config.flowControl = static_cast<FlowControl>(flowTemp);

        // Line State
        if (!rdr.readBool(dtr))                                     { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(dsr))                                     { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(rts))                                     { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(cts))                                     { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(txd))                                     { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(rxd))                                     { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(dcd))                                     { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(ri))                                      { rdr.exitChunkPayload(chunk); return false; }

        // Timing
        if (!rdr.readF64(clockHz))                                  { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readF64(cyclesPerBit))                             { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU64(cycleAccumulator))                         { rdr.exitChunkPayload(chunk); return false; }

        // TX Engine
        uint8_t txTemp = 0;
        if (!rdr.readU8(txTemp))                                    { rdr.exitChunkPayload(chunk); return false; }
        if (txTemp > static_cast<uint8_t>(TxState::StopBit))        { rdr.exitChunkPayload(chunk); return false; }

        txState = static_cast<TxState>(txTemp);

        if (!rdr.readF64(txCountdown))                              { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU8(txShift))                                   { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU8(txOriginalByte))                            { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readI32(txBitIndex))                               { rdr.exitChunkPayload(chunk); return false; }

        // Rx Engine
        uint8_t rxTemp = 0;
        if (!rdr.readU8(rxTemp))                                    { rdr.exitChunkPayload(chunk); return false; }
        if (rxTemp > static_cast<uint8_t>(RxState::StopBit))        { rdr.exitChunkPayload(chunk); return false; }

        rxState = static_cast<RxState>(rxTemp);

        if (!rdr.readF64(rxCountdown))                              { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU8(rxShift))                                   { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readI32(rxBitIndex))                               { rdr.exitChunkPayload(chunk); return false; }

        if (!rdr.readBool(lastRXD))                                 { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(parityError))                             { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(framingError))                            { rdr.exitChunkPayload(chunk); return false; }

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

        if (!readByteQueue(txBytes))                                { rdr.exitChunkPayload(chunk); return false; }
        if (!readByteQueue(rxBytes))                                { rdr.exitChunkPayload(chunk); return false; }

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

    rxStartPending      = false;

    parityError         = false;
    framingError        = false;

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

void RS232Device::clearReceiveErrors()
{
    parityError     = false;
    framingError    = false;
}

void RS232Device::tick(uint32_t cyclesElapsed)
{
    if (cyclesElapsed == 0)
        return;

    tickRX(cyclesElapsed);
    tickTX(cyclesElapsed);

    if (!endpoint)
        return;

    cycleAccumulator += cyclesElapsed;

    constexpr uint64_t EndpointPollCycles = 512;

    if (cycleAccumulator < EndpointPollCycles)
        return;

    cycleAccumulator %= EndpointPollCycles;

    endpoint->tick();

    uint8_t value = 0;

    while (endpoint->readByte(value))
        queueTransmitByte(value);
}

void RS232Device::setTXD(bool state)
{
    txd = state;

    if (!peer)
        return;

    peer->rxd = state;
}

void RS232Device::setRXD(bool state)
{
    if (rxd && !state)
        rxStartPending = true;

    rxd = state;
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

void RS232Device::setBaud(uint32_t baud)
{
    if (baud == 0)
        return;

    config.baud = baud;
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
                if (config.flowControl == FlowControl::RTS_CTS && !cts)
                    break;

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
            if (rxStartPending)
            {
                rxStartPending = false;

                rxState = RxState::StartBit;
                rxCountdown = cyclesPerBit * 0.5;

                rxShift = 0;
                rxBitIndex = 0;
            }

            break;
        }

        case RxState::StartBit:
        {
            rxCountdown -= static_cast<double>(cyclesElapsed);

            if (rxCountdown <= 0.0)
            {
                // Valid start bit should still be low at its center.
                if (!rxd)
                {
                    rxState = RxState::DataBits;

                    // First data bit is one full bit-time after
                    // the center of the start bit.
                    rxCountdown += cyclesPerBit;
                }
                else
                {
                    // False start/glitch.
                    rxState = RxState::Idle;
                    rxCountdown = 0.0;
                    rxShift = 0;
                    rxBitIndex = 0;
                }
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
                    parityError = true;

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
                {
                    rxBytes.push(rxShift);

                    if (endpoint)
                        endpoint->writeByte(rxShift);
                }
                else
                {
                    framingError = true;
                }

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
    if (config.parity == Parity::Mark)
        return true;

    if (config.parity == Parity::Space)
        return false;

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

    out << "  Endpoint: " << (endpoint ? "attached" : "none") << "\n";

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
        case RxState::Idle:      out << "Idle"; break;
        case RxState::StartBit:  out << "StartBit"; break;
        case RxState::DataBits:  out << "DataBits"; break;
        case RxState::ParityBit: out << "ParityBit"; break;
        case RxState::StopBit:   out << "StopBit"; break;
    }

    out << " bit=" << int(rxBitIndex)
        << " shift=$" << std::hex << std::uppercase << int(rxShift)
        << std::dec
        << " queued=" << rxBytes.size()
        << " baud=" << config.baud
        << "\n";

    out << "  RX Errors: "
    << "Parity=" << (parityError ? "Yes" : "No") << " "
    << "Framing=" << (framingError ? "Yes" : "No")
    << "\n";

   return out.str();
}

std::string RS232Device::selfTest(uint8_t testByte, Parity parity)
{
    std::ostringstream out;

    RS232Device testPeer;

    const RS232Config oldConfig = config;

    RS232Endpoint* oldEndpoint = endpoint;
    detachEndpoint();

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

    setConfig(oldConfig);
    attachEndpoint(oldEndpoint);

    return out.str();
}

std::string RS232Device::selfTestMulti()
{
    std::ostringstream out;

    RS232Device testPeer;

    const RS232Config oldConfig = config;
    RS232Device* oldPeer = peer;

    RS232Endpoint* oldEndpoint = endpoint;
    detachEndpoint();

    RS232Config testConfig = config;
    testConfig.parity = Parity::None;
    testConfig.flowControl = FlowControl::None;

    setConfig(testConfig);

    testPeer.setClockRate(clockHz);
    testPeer.setConfig(testConfig);

    attachPeerDevice(&testPeer);
    testPeer.attachPeerDevice(this);

    reset();
    testPeer.reset();

    const uint8_t testBytes[] =
    {
        0x55,
        0xAA,
        0x00,
        0xFF,
        0x42
    };

    constexpr size_t testCount = sizeof(testBytes) / sizeof(testBytes[0]);

    for (uint8_t value : testBytes)
        queueTransmitByte(value);

    const uint32_t parityBits = (config.parity == Parity::None) ? 0u : 1u;
    const uint32_t frameBits = 1u + static_cast<uint32_t>(config.dataBits) + parityBits + static_cast<uint32_t>(config.stopBits);
    const uint32_t maxCycles = static_cast<uint32_t>(cyclesPerBit * frameBits * testCount * 2.0);
    uint8_t received[testCount] = {};
    size_t receivedCount = 0;

    for (uint32_t cycle = 0;
         cycle < maxCycles && receivedCount < testCount;
         ++cycle)
    {
        tick(1);
        testPeer.tick(1);

        uint8_t value = 0;

        while (testPeer.popReceivedByte(value))
        {
            if (receivedCount < testCount)
                received[receivedCount++] = value;
        }
    }

    out << "RS232 Multi-Byte Test\n";
    out << "---------------------\n";

    bool passed = receivedCount == testCount;

    for (size_t i = 0; i < testCount; ++i)
    {
        out << "Byte " << i << ": sent=$"
            << std::hex
            << std::uppercase
            << std::setw(2)
            << std::setfill('0')
            << static_cast<int>(testBytes[i]);

        if (i < receivedCount)
        {
            out << " received=$"
                << std::setw(2)
                << static_cast<int>(received[i]);

            if (received[i] != testBytes[i])
                passed = false;
        }
        else
        {
            out << " received=none";
            passed = false;
        }

        out << "\n";
    }

    out << "Result: "
        << (passed ? "PASS" : "FAIL")
        << "\n";

    out << std::dec << std::setfill(' ');

    attachPeerDevice(oldPeer);
    testPeer.attachPeerDevice(nullptr);
    attachEndpoint(oldEndpoint);

    setConfig(oldConfig);

    return out.str();
}

std::string RS232Device::selfTestFormats()
{
    std::ostringstream out;

    struct FormatTest
    {
        const char* name;
        uint8_t dataBits;
        Parity parity;
        uint8_t stopBits;
    };

    const FormatTest tests[] =
    {
        { "8N1", 8, Parity::None, 1 },
        { "8E1", 8, Parity::Even, 1 },
        { "8O1", 8, Parity::Odd,  1 },
        { "7E1", 7, Parity::Even, 1 },
        { "7O1", 7, Parity::Odd,  1 },
        { "8N2", 8, Parity::None, 2 }
    };

    const uint8_t testBytes[] =
    {
        0x55,
        0x2A,
        0x00,
        0x7F
    };

    const RS232Config oldConfig = config;
    RS232Device* oldPeer = peer;

    RS232Endpoint* oldEndpoint = endpoint;
    detachEndpoint();

    out << "RS232 Format Test\n";
    out << "-----------------\n";

    bool allPassed = true;

    for (const auto& test : tests)
    {
        RS232Device testPeer;

        RS232Config testConfig = oldConfig;
        testConfig.dataBits = test.dataBits;
        testConfig.parity = test.parity;
        testConfig.stopBits = test.stopBits;
        testConfig.flowControl = FlowControl::None;

        setConfig(testConfig);

        testPeer.setClockRate(clockHz);
        testPeer.setConfig(testConfig);

        attachPeerDevice(&testPeer);
        testPeer.attachPeerDevice(this);

        reset();
        testPeer.reset();

        for (uint8_t value : testBytes)
            queueTransmitByte(value);

        const uint32_t parityBits = (testConfig.parity == Parity::None) ? 0u : 1u;
        const uint32_t frameBits = 1u + static_cast<uint32_t>(testConfig.dataBits) + parityBits + static_cast<uint32_t>(testConfig.stopBits);
        constexpr size_t testCount = sizeof(testBytes) / sizeof(testBytes[0]);
        const uint32_t maxCycles = static_cast<uint32_t>(cyclesPerBit * frameBits * testCount * 2.0);
        uint8_t received[testCount] = {};
        size_t receivedCount = 0;

        for (uint32_t cycle = 0;
             cycle < maxCycles && receivedCount < testCount;
             ++cycle)
        {
            tick(1);
            testPeer.tick(1);

            uint8_t value = 0;

            while (testPeer.popReceivedByte(value))
            {
                if (receivedCount < testCount)
                    received[receivedCount++] = value;
            }
        }

        bool passed = receivedCount == testCount;

        for (size_t i = 0; i < testCount && passed; ++i)
        {
            uint8_t expected = testBytes[i];

            if (test.dataBits == 7)
                expected &= 0x7F;

            if (received[i] != expected)
                passed = false;
        }

        out << test.name
            << ": "
            << (passed ? "PASS" : "FAIL")
            << "\n";

        if (!passed)
            allPassed = false;

        testPeer.detachPeerDevice();
        detachPeerDevice();
    }

    out << "Overall: "
        << (allPassed ? "PASS" : "FAIL")
        << "\n";

    attachPeerDevice(oldPeer);
    setConfig(oldConfig);
    attachEndpoint(oldEndpoint);

    return out.str();
}

std::string RS232Device::selfTestFlowControl()
{
    std::ostringstream out;

    const RS232Config oldConfig = config;
    RS232Device* oldPeer = peer;

    RS232Endpoint* oldEndpoint = endpoint;
    detachEndpoint();

    RS232Device testPeer;

    RS232Config testConfig = config;
    testConfig.parity = Parity::None;
    testConfig.flowControl = FlowControl::RTS_CTS;

    setConfig(testConfig);

    testPeer.setClockRate(clockHz);
    testPeer.setConfig(testConfig);

    attachPeerDevice(&testPeer);
    testPeer.attachPeerDevice(this);

    reset();
    testPeer.reset();

    constexpr uint8_t testByte = 0x55;

    out << "RS232 Flow Control Test\n";
    out << "-----------------------\n";

    /*
     * Peer lowers RTS.
     *
     * Because RTS on testPeer is connected to CTS on this
     * device, our CTS should now become false.
     */
    testPeer.setRTS(false);

    const bool ctsBlocked = !getCTS();

    /*
     * Queue a byte while CTS is low.
     */
    queueTransmitByte(testByte);

    /*
     * Tick long enough that the byte definitely would have
     * transmitted if CTS were being ignored.
     */
    const uint32_t parityBits = (config.parity == Parity::None) ? 0u : 1u;
    const uint32_t frameBits = 1u + static_cast<uint32_t>(config.dataBits) + parityBits + static_cast<uint32_t>(config.stopBits);
    const uint32_t blockedCycles = static_cast<uint32_t>(cyclesPerBit * static_cast<double>(frameBits) * 2.0);

    for (uint32_t cycle = 0; cycle < blockedCycles; ++cycle)
    {
        tick(1);
        testPeer.tick(1);
    }

    /*
     * No byte should have reached the peer.
     */
    uint8_t receivedValue = 0;

    const bool receivedWhileBlocked = testPeer.popReceivedByte(receivedValue);
    const bool transmitHeld = !receivedWhileBlocked && !isTransmitIdle();

    /*
     * Release CTS by raising peer RTS.
     */
    testPeer.setRTS(true);

    const bool ctsReleased = getCTS();

    /*
     * Now allow enough time for the queued byte to transmit.
     */
    const uint32_t transmitCycles = static_cast<uint32_t>(cyclesPerBit * static_cast<double>(frameBits) * 2.0);
    bool receivedAfterRelease = false;

    for (uint32_t cycle = 0;
         cycle < transmitCycles && !receivedAfterRelease;
         ++cycle)
    {
        tick(1);
        testPeer.tick(1);

        if (testPeer.popReceivedByte(receivedValue))
            receivedAfterRelease = true;
    }

    const bool correctByte = receivedAfterRelease && receivedValue == testByte;
    const bool passed = ctsBlocked && transmitHeld && ctsReleased && correctByte;

    out << "CTS low detected:       "
        << (ctsBlocked ? "PASS" : "FAIL")
        << "\n";

    out << "TX blocked by CTS:      "
        << (transmitHeld ? "PASS" : "FAIL")
        << "\n";

    out << "CTS high detected:      "
        << (ctsReleased ? "PASS" : "FAIL")
        << "\n";

    out << "Queued byte transmitted:"
        << (correctByte ? " PASS" : " FAIL")
        << "\n";

    if (receivedAfterRelease)
    {
        out << "Received:              $"
            << std::hex
            << std::uppercase
            << std::setw(2)
            << std::setfill('0')
            << static_cast<int>(receivedValue)
            << "\n";
    }
    else
    {
        out << "Received:              none\n";
    }

    out << "Overall:                "
        << (passed ? "PASS" : "FAIL")
        << "\n";

    out << std::dec << std::setfill(' ');

    /*
     * Disconnect the temporary peer before it is destroyed.
     */
    testPeer.detachPeerDevice();
    detachPeerDevice();

    /*
     * Restore the device's original connection/configuration.
     */
    attachPeerDevice(oldPeer);
    setConfig(oldConfig);
    attachEndpoint(oldEndpoint);

    return out.str();
}

std::string RS232Device::selfTestErrors()
{
    std::ostringstream out;

    const RS232Config oldConfig = config;
    RS232Device* oldPeer = peer;

    RS232Endpoint* oldEndpoint = endpoint;
    detachEndpoint();

    RS232Device testPeer;

    RS232Config testConfig = config;
    testConfig.dataBits = 8;
    testConfig.stopBits = 1;
    testConfig.parity = Parity::Even;
    testConfig.flowControl = FlowControl::None;

    setConfig(testConfig);

    testPeer.setClockRate(clockHz);
    testPeer.setConfig(testConfig);

    attachPeerDevice(&testPeer);
    testPeer.attachPeerDevice(this);

    reset();
    testPeer.reset();

    out << "RS232 Error Handling Test\n";
    out << "-------------------------\n";

    bool parityTestPassed = false;
    bool framingTestPassed = false;
    bool stickyTestPassed = false;
    bool clearTestPassed = false;

    /*
     * ---------------------------------------------------------
     * 1. PARITY ERROR TEST
     * ---------------------------------------------------------
     */

    constexpr uint8_t parityByte = 0x55;

    queueTransmitByte(parityByte);

    bool parityBitCorrupted = false;

    const uint32_t parityTimeout = static_cast<uint32_t>(cyclesPerBit * 20.0);

    for (uint32_t cycle = 0; cycle < parityTimeout; ++cycle)
    {
        tick(1);

        /*
         * Once the transmitter enters its parity bit,
         * deliberately force the receiver's RXD line to
         * the opposite value.
         */
        if (txState == TxState::ParityBit && !parityBitCorrupted)
        {
            const bool correctParity = calculateParity(parityByte);
            testPeer.setRXD(!correctParity);
            parityBitCorrupted = true;
        }

        testPeer.tick(1);

        if (testPeer.hasParityError())
            break;
    }

    parityTestPassed = parityBitCorrupted && testPeer.hasParityError();

    /*
     * Clean up serial state before next test.
     */
    reset();
    testPeer.reset();

    /*
     * ---------------------------------------------------------
     * 2. FRAMING ERROR TEST
     * ---------------------------------------------------------
     */

    constexpr uint8_t framingByte = 0xAA;

    /*
     * Disable parity so the stop-bit test is isolated.
     */
    testConfig.parity = Parity::None;

    setConfig(testConfig);
    testPeer.setConfig(testConfig);

    queueTransmitByte(framingByte);

    bool stopBitCorrupted = false;

    const uint32_t framingTimeout = static_cast<uint32_t>(cyclesPerBit * 20.0);

    for (uint32_t cycle = 0; cycle < framingTimeout; ++cycle)
    {
        tick(1);

        if (txState == TxState::StopBit && !stopBitCorrupted)
        {
            /*
             * Stop bit must be high.
             * Force it low at the receiver.
             */
            testPeer.setRXD(false);
            stopBitCorrupted = true;
        }

        testPeer.tick(1);

        if (testPeer.hasFramingError())
            break;
    }

    framingTestPassed = stopBitCorrupted && testPeer.hasFramingError();

    /*
     * ---------------------------------------------------------
     * 3. STICKY ERROR TEST
     * ---------------------------------------------------------
     *
     * Send a valid frame after the framing error.
     * The framing flag should remain set.
     */

    testPeer.setRXD(true);

    constexpr uint8_t validByte = 0x42;

    queueTransmitByte(validByte);

    const uint32_t validTimeout = static_cast<uint32_t>(cyclesPerBit * 20.0);

    bool receivedValidByte = false;
    uint8_t received = 0;

    for (uint32_t cycle = 0; cycle < validTimeout; ++cycle)
    {
        tick(1);
        testPeer.tick(1);

        if (testPeer.popReceivedByte(received))
        {
            receivedValidByte = true;
            break;
        }
    }

    stickyTestPassed = receivedValidByte && received == validByte && testPeer.hasFramingError();

    /*
     * ---------------------------------------------------------
     * 4. CLEAR ERROR TEST
     * ---------------------------------------------------------
     */

    testPeer.clearReceiveErrors();

    clearTestPassed = !testPeer.hasParityError() && !testPeer.hasFramingError();

    const bool passed = parityTestPassed && framingTestPassed && stickyTestPassed && clearTestPassed;

    out << "Parity error detected:  "
        << (parityTestPassed ? "PASS" : "FAIL")
        << "\n";

    out << "Framing error detected: "
        << (framingTestPassed ? "PASS" : "FAIL")
        << "\n";

    out << "Flags remain sticky:    "
        << (stickyTestPassed ? "PASS" : "FAIL")
        << "\n";

    out << "Clear errors works:     "
        << (clearTestPassed ? "PASS" : "FAIL")
        << "\n";

    out << "Overall:                "
        << (passed ? "PASS" : "FAIL")
        << "\n";

    testPeer.detachPeerDevice();
    detachPeerDevice();

    attachPeerDevice(oldPeer);
    setConfig(oldConfig);
    attachEndpoint(oldEndpoint);

    return out.str();
}
