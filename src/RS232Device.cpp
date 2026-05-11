// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "RS232Device.h"

RS232Device::RS232Device() :
    cycleAccumulator(0),
    rxBitIndex(0),
    rxShift(0),
    receiving(false),
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
    txBitIndex(0),
    rxCountdown(0.0),
    rxState(RxState::Idle)
{

}

RS232Device::~RS232Device() = default;

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
                    // Stop bit is high.
                    setTXD(true);
                    txCountdown += cyclesPerBit * config.stopBits;
                    txState = TxState::StopBit;
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
                    rxState = RxState::StopBit;
                    rxCountdown += cyclesPerBit;
                    break;
                }

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
