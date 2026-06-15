// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "CIA2.h"
#include "CPU.h"

CIA2::CIA2() :
    cpu(nullptr),
    bus(nullptr),
    logger(nullptr),
    rs232dev(nullptr),
    vic(nullptr),
    iecProtocolEnabled(false)
{
    setMode(VideoMode::NTSC);
}

CIA2::~CIA2() = default;

void CIA2::saveState(StateWriter& wrtr) const
{
    // CIA2 = "Core" and Registers
    wrtr.beginChunk("CIA2");
    wrtr.writeU32(1); // version

    // Base save
    saveBaseState(wrtr);

    // End the chunk for CIA1
    wrtr.endChunk();

    // Write CI2X chunk for runtime status
    wrtr.beginChunk("CI2X");
    wrtr.writeU32(1); // version

    // Base runtime state
    saveBaseRuntimeState(wrtr);

    // Dump IEC
    wrtr.writeBool(iecProtocolEnabled);
    wrtr.writeU8(deviceNumber);
    wrtr.writeBool(listening);
    wrtr.writeBool(talking);
    wrtr.writeU8(currentSecondaryAddress);
    wrtr.writeBool(atnLine);
    wrtr.writeBool(atnHandshakePending);
    wrtr.writeBool(atnHandshakeJustCleared);
    wrtr.writeBool(lastAtnLevel);
    wrtr.writeBool(lastClk);
    wrtr.writeBool(lastSrqLevel);
    wrtr.writeBool(lastDataLevel);
    wrtr.writeU8(iecCmdShiftReg);
    wrtr.writeI32(iecCmdBitCount);

    // End the chunk
    wrtr.endChunk();
}

bool CIA2::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "CIA2", 4) == 0)
    {
        rdr.enterChunkPayload(chunk);

        uint32_t ver = 0;
        if (!rdr.readU32(ver))                                              { rdr.exitChunkPayload(chunk); return false; }
        if (ver != 1)                                                       { rdr.exitChunkPayload(chunk); return false; }

        // Load base state
        if (!loadBaseState(rdr))                                        { rdr.exitChunkPayload(chunk); return false; }

        // Normalize
        if (rs232dev)
            updateRS232Outputs();

        recomputeIEC();

        // End chunk
        rdr.exitChunkPayload(chunk);
        return true;
    }

    if (std::memcmp(chunk.tag, "CI2X", 4) == 0)
    {
        rdr.enterChunkPayload(chunk);

        uint32_t ver = 0;
        if (!rdr.readU32(ver))                                              { rdr.exitChunkPayload(chunk); return false; }
        if (ver != 1)                                                       { rdr.exitChunkPayload(chunk); return false; }

        if (!loadBaseRuntimeState(rdr))                                 { rdr.exitChunkPayload(chunk); return false; }

        // Load IEC
        if (!rdr.readBool(iecProtocolEnabled))                              { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU8(deviceNumber))                                      { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(listening))                                       { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(talking))                                         { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU8(currentSecondaryAddress))                           { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(atnLine))                                         { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(atnHandshakePending))                             { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(atnHandshakeJustCleared))                         { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(lastAtnLevel))                                    { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(lastClk))                                         { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(lastSrqLevel))                                    { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(lastDataLevel))                                   { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU8(iecCmdShiftReg))                                    { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readI32(iecCmdBitCount))                                   { rdr.exitChunkPayload(chunk); return false; }

        // Normalize
        if (iecCmdBitCount < 0) iecCmdBitCount = 0;
        if (iecCmdBitCount > 8) iecCmdBitCount = 8;
        recomputeIEC();

        rdr.exitChunkPayload(chunk);
        return true;
    }

    // Unknown chunk
    return false;
}

void CIA2::reset()
{
    CIA6526::reset();

    // Reset NMI status
    nmiAsserted                 = false;

    if (cpu)
        cpu->setNMILine(false);

    // IEC
    deviceNumber                = 0xFF;
    listening                   = false;
    talking                     = false;
    currentSecondaryAddress     = 0xFF;
    atnLine                     = false;
    atnHandshakePending         = false;
    atnHandshakeJustCleared     = false;
    lastAtnLevel                = false;
    lastClk                     = false;
    lastSrqLevel                = false;
    lastDataLevel               = true;
    iecCmdShiftReg              = 0;
    iecCmdBitCount              = 0;

    recomputeIEC();
}

uint16_t CIA2::getCurrentVICBank() const
{
    // PA0/PA1 select VIC bank, inverted by CIA2 wiring.
    // Inputs float high; outputs drive from the port A latch.
    const uint8_t effectivePA = getPortAOutput();

    const uint8_t bankBits = static_cast<uint8_t>((~effectivePA) & 0x03);

    return static_cast<uint16_t>(bankBits) * 0x4000;
}

uint8_t CIA2::readPortA()
{
    uint8_t result = getPortAOutput();

    // Always sample IEC input wires for PA6/PA7 (BIT $DD00 polls these)
    if (bus)
    {
        const bool clkHigh  = bus->readClkLine();   // true = wire high (released)
        const bool dataHigh = bus->readDataLine();  // true = wire high (released)

        if (clkHigh)  result |= MASK_CLK_IN;  else result &= ~MASK_CLK_IN;
        if (dataHigh) result |= MASK_DATA_IN; else result &= ~MASK_DATA_IN;
    }

    return result;
}

uint8_t CIA2::readPortB()
{
    uint8_t result = getPortBOutput();

    auto applyInputBit = [&](uint8_t mask, bool high)
    {
        if (!isPortBOutput(mask))
        {
            if (high)
                result |= mask;
            else
                result &= static_cast<uint8_t>(~mask);
        }
    };

    applyInputBit(RXD_MASK, rs232dev ? rs232dev->getRXD() : true);
    applyInputBit(DSR_MASK, rs232dev ? rs232dev->getDSR() : true);
    applyInputBit(CTS_MASK, rs232dev ? rs232dev->getCTS() : true);
    applyInputBit(DCD_MASK, rs232dev ? rs232dev->getDCD() : true);
    applyInputBit(RI_MASK,  rs232dev ? rs232dev->getRI()  : true);

    return result;
}

void CIA2::portAOutputChanged(uint8_t value)
{
    updateRS232Outputs();
    recomputeIEC();

    TraceManager* traceMgr = getTraceManager();
    if (traceMgr && traceMgr->ciaDetailOn(2, TraceManager::TraceDetail::CIA_IEC))
    {
        std::ostringstream out;
        out << "[CIA2:IEC] PRA write=$"
            << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << int(value);
        traceMgr->recordCustomEvent(out.str(), makeCIAStamp());
    }
}

void CIA2::portBOutputChanged(uint8_t value)
{
    updateRS232Outputs();
}

void CIA2::postTimerUpdates(uint32_t cyclesElapsed)
{
    // Update user-port RS232 device timing
    if (rs232dev)
        rs232dev->tick(cyclesElapsed);
}


void CIA2::irqLineChanged(bool active)
{
    nmiAsserted = active;
    if (cpu) cpu->setNMILine(active);
}

void CIA2::clkChanged(bool level)
{
    bool falling = (lastClk && !level);
    bool rising  = (!lastClk && level);
    lastClk      = level;

    if (!iecProtocolEnabled) return;

    if (!bus) return;

    // --- ATN LOW: command / secondary bytes from the C64 ---
    if (atnLine)
    {
        // Handshake: swallow *only the first rising edge* after ATN is asserted
        if (atnHandshakePending && rising)
        {
            atnHandshakePending = false;
            atnHandshakeJustCleared = true;
            return;
        }

        // After handshake is cleared, only FALLING edges carry command bits
        if (!atnHandshakePending && falling)
        {
            if (atnHandshakeJustCleared)
            {
                // Ignore this first post-handshake edge (bus not ready)
                atnHandshakeJustCleared = false;
                return;
            }

            bool dataHigh = bus->readDataLine(); // true = logical '1'

            // Build the command byte as LSB-first:
            if (dataHigh)
            {
                iecCmdShiftReg |= static_cast<uint8_t>(1u << iecCmdBitCount);
            }
            ++iecCmdBitCount;

            if (iecCmdBitCount == 8)
            {
                uint8_t cmd = iecCmdShiftReg;

                decodeIECCommand(cmd);

                TraceManager* traceMgr = getTraceManager();
                if (traceMgr && traceMgr->ciaDetailOn(2, TraceManager::TraceDetail::CIA_IEC))
                {
                    std::ostringstream out;
                    out << "[CIA2:IEC] CMD byte=$"
                        << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << int(cmd);
                    traceMgr->recordCustomEvent(out.str(), makeCIAStamp());
                }

                iecCmdShiftReg = 0;
                iecCmdBitCount = 0;
            }

            // While ATN is low we *only* care about command bytes, not data
            return;
        }

        // ATN low but no falling edge carrying a bit: nothing else to do
        return;
    }
}

void CIA2::dataChanged(bool state)
{
    lastDataLevel = state;

    TraceManager* traceMgr = getTraceManager();
    if (traceMgr && traceMgr->ciaDetailOn(2, TraceManager::TraceDetail::CIA_IEC))
    {
        std::ostringstream out;
        out << "[CIA2:IEC] DATA=" << (state ? "H" : "L");
        traceMgr->recordCustomEvent(out.str(), makeCIAStamp());
    }
}

void CIA2::atnChanged(bool assertedLow)
{
    bool fallingEdge = !lastAtnLevel && assertedLow;

    atnLine = assertedLow;
    lastAtnLevel = assertedLow;

    TraceManager* traceMgr = getTraceManager();
    if (traceMgr && traceMgr->ciaDetailOn(2, TraceManager::TraceDetail::CIA_IEC))
    {
        std::ostringstream out;
        out << "[CIA2:IEC] ATN=" << (assertedLow ? "L" : "H")
            << " falling=" << (fallingEdge ? "Y" : "N")
            << " listen=" << (listening ? "Y" : "N")
            << " talk=" << (talking ? "Y" : "N");
        traceMgr->recordCustomEvent(out.str(), makeCIAStamp());
    }

    if (!iecProtocolEnabled) return;

    if (fallingEdge)
    {
        currentSecondaryAddress  = 0xFF;
        listening                = false;
        talking                  = false;

        atnHandshakePending      = true;
        atnHandshakeJustCleared  = false;
        iecCmdShiftReg           = 0;
        iecCmdBitCount           = 0;

        if (bus)
            lastClk = bus->readClkLine();
        else
            lastClk = true;
    }
    else if (!assertedLow)
    {
        // ATN released
        atnHandshakePending      = false;
        atnHandshakeJustCleared  = false;
        iecCmdShiftReg           = 0;
        iecCmdBitCount           = 0;
    }
}

void CIA2::srqChanged(bool level)
{
    const bool falling = lastSrqLevel && !level;

    TraceManager* traceMgr = getTraceManager();
    if (traceMgr && traceMgr->ciaDetailOn(2, TraceManager::TraceDetail::CIA_IEC))
    {
        std::ostringstream out;
        out << "[CIA2:IEC] SRQ=" << (level ? "H" : "L")
            << " falling=" << (falling ? "Y" : "N");

        traceMgr->recordCustomEvent(out.str(), makeCIAStamp());
    }

    if (falling)
    {
        triggerInterrupt(INTERRUPT_FLAG_LINE);
    }

    lastSrqLevel = level;
}

void CIA2::decodeIECCommand(uint8_t cmd)
{
    uint8_t code = cmd & 0xF0;
    uint8_t low  = cmd & 0x1F;  // device # or SA (0-31)

    auto traceIec = [&](const std::string& text)
    {
        TraceManager* traceMgr = getTraceManager();
        if (traceMgr && traceMgr->ciaDetailOn(2, TraceManager::TraceDetail::CIA_IEC))
            traceMgr->recordCustomEvent(text, makeCIAStamp());
    };

    switch (code)
    {
        case 0x20: // LISTEN + device
        {
            deviceNumber = low;
            listening    = false;   // from C64 perspective
            talking      = true;    // C64 talks, device listens
            currentSecondaryAddress = 0xFF;

            if (bus)
                bus->listen(deviceNumber);

            {
                std::ostringstream out;
                out << "[CIA2:IEC] LISTEN dev=" << std::dec << int(deviceNumber);
                traceIec(out.str());
            }
            break;
        }

        case 0x40: // TALK + device
        {
            deviceNumber = low;
            talking      = false;   // from C64 perspective
            listening    = true;    // C64 listens, device talks
            currentSecondaryAddress = 0xFF;

            if (bus)
                bus->talk(deviceNumber);

            {
                std::ostringstream out;
                out << "[CIA2:IEC] TALK dev=" << std::dec << int(deviceNumber);
                traceIec(out.str());
            }
            break;
        }

        case 0x60: // SECONDARY ADDRESS / DATA
        {
            uint8_t sa = cmd & 0x0F;
            currentSecondaryAddress = sa;

            if (bus && deviceNumber != 0xFF)
                bus->secondaryAddress(deviceNumber, currentSecondaryAddress);

            {
                std::ostringstream out;
                out << "[CIA2:IEC] SECONDARY dev=" << std::dec << int(deviceNumber)
                    << " sa=" << int(currentSecondaryAddress);
                traceIec(out.str());
            }
            break;
        }

        case 0xC0: // OPEN for TALK
        {
            uint8_t sa = cmd & 0x0F;
            currentSecondaryAddress = sa;

            if (bus && deviceNumber != 0xFF)
                bus->secondaryAddress(deviceNumber, currentSecondaryAddress);

            {
                std::ostringstream out;
                out << "[CIA2:IEC] OPEN-TALK dev=" << std::dec << int(deviceNumber)
                    << " sa=" << int(currentSecondaryAddress);
                traceIec(out.str());
            }
            break;
        }

        case 0xF0: // OPEN + secondary address
        {
            uint8_t sa = cmd & 0x0F;
            currentSecondaryAddress = sa;

            if (bus && deviceNumber != 0xFF)
                bus->secondaryAddress(deviceNumber, currentSecondaryAddress);

            {
                std::ostringstream out;
                out << "[CIA2:IEC] OPEN dev=" << std::dec << int(deviceNumber)
                    << " sa=" << int(currentSecondaryAddress);
                traceIec(out.str());
            }
            break;
        }

        case 0xE0: // CLOSE + secondary address
        {
            uint8_t sa = cmd & 0x0F;
            currentSecondaryAddress = sa;

            {
                std::ostringstream out;
                out << "[CIA2:IEC] CLOSE dev=" << std::dec << int(deviceNumber)
                    << " sa=" << int(currentSecondaryAddress);
                traceIec(out.str());
            }
            break;
        }

        default:
        {
            if (cmd == 0x3F) // UNLISTEN
            {
                listening = false;

                if (bus && deviceNumber != 0xFF)
                    bus->unListen(deviceNumber);

                {
                    std::ostringstream out;
                    out << "[CIA2:IEC] UNLISTEN dev=" << std::dec << int(deviceNumber);
                    traceIec(out.str());
                }
            }
            else if (cmd == 0x5F) // UNTALK
            {
                talking = false;

                if (bus && deviceNumber != 0xFF)
                    bus->unTalk(deviceNumber);

                {
                    std::ostringstream out;
                    out << "[CIA2:IEC] UNTALK dev=" << std::dec << int(deviceNumber);
                    traceIec(out.str());
                }
            }
            else
            {
                std::ostringstream out;
                out << "[CIA2:IEC] UNKNOWN cmd=$"
                    << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << int(cmd);
                traceIec(out.str());
            }
            break;
        }
    }
}

std::string CIA2::dumpRegisters(const std::string& group) const
{
    std::stringstream out;

    // First dump the generic 6526 registers from the base class.
    out << CIA6526::dumpRegisters(group);

    // CIA2-specific RS232 mapping
    if (group == "rs232" || group == "serial" || group == "all")
    {
        out << "\nCIA2 RS232 mapping\n\n";

        out << "  PA2 TXD: "
            << (isPortAOutput(TXD_MASK) ? "output" : "input")
            << " latch="
            << (getPortALatchBit(TXD_MASK) ? "H" : "L")
            << "\n";

        out << "  PB1 RTS: "
            << (isPortBOutput(RTS_MASK) ? "output" : "input")
            << " latch="
            << (getPortBLatchBit(RTS_MASK) ? "H" : "L")
            << "\n";

        out << "  PB2 DTR: "
            << (isPortBOutput(DTR_MASK) ? "output" : "input")
            << " latch="
            << (getPortBLatchBit(DTR_MASK) ? "H" : "L")
            << "\n";

        if (rs232dev)
        {
            out << "\n";
            out << rs232dev->debugString();
        }
        else
        {
            out << "\nRS232 Device: none attached\n";
        }
    }

    // CIA2-specific VIC bank info
    if (group == "vic" || group == "all")
    {
        out << "\nVIC-II Bank Control\n\n";
        out << "Current VIC Bank = $"
            << std::setw(4) << getCurrentVICBank()
            << "\n";

        out << "PA0 Bank Bit 0 latch = "
            << (getPortALatchBit(VIC_BANK0) ? "1" : "0")
            << "  direction="
            << (isPortAOutput(VIC_BANK0) ? "output" : "input")
            << "\n";

        out << "PA1 Bank Bit 1 latch = "
            << (getPortALatchBit(VIC_BANK1) ? "1" : "0")
            << "  direction="
            << (isPortAOutput(VIC_BANK1) ? "output" : "input")
            << "\n";
    }

    // CIA2-specific IEC state
    if (group == "iec" || group == "all")
    {
        out << "\nLegacy/software IEC state\n\n";

        out << "Device Number = "
            << std::dec << static_cast<int>(deviceNumber)
            << std::hex << "\n";

        out << "Legacy C64 talker flag   = "
            << (talking ? "Yes" : "No") << "\n";

        out << "Legacy C64 listener flag = "
            << (listening ? "Yes" : "No") << "\n";

        out << "ATN Line = "
            << (atnLine ? "Asserted (low)" : "Released (high)")
            << "\n";

        out << "\nCIA2 IEC output mapping\n";
        out << "  PA3 ATN out  latch="
            << (getPortALatchBit(MASK_ATN_OUT) ? "H" : "L")
            << " direction="
            << (isPortAOutput(MASK_ATN_OUT) ? "output" : "input")
            << "\n";

        out << "  PA4 CLK out  latch="
            << (getPortALatchBit(MASK_CLK_OUT) ? "H" : "L")
            << " direction="
            << (isPortAOutput(MASK_CLK_OUT) ? "output" : "input")
            << "\n";

        out << "  PA5 DATA out latch="
            << (getPortALatchBit(MASK_DATA_OUT) ? "H" : "L")
            << " direction="
            << (isPortAOutput(MASK_DATA_OUT) ? "output" : "input")
            << "\n";

        if (bus)
        {
            out << "\nCIA2 IEC input lines\n";
            out << "  PA6 CLK in  = "
                << (bus->readClkLine() ? "High/released" : "Low/asserted")
                << "\n";

            out << "  PA7 DATA in = "
                << (bus->readDataLine() ? "High/released" : "Low/asserted")
                << "\n";

            out << "  SRQ         = "
                << (bus->readSrqLine() ? "High/released" : "Low/asserted")
                << "\n";
        }
        else
        {
            out << "\nIEC Bus: none attached\n";
        }
    }

    // CIA2-specific interrupt output line.
    // The base owns IFR/IER. CIA2 only owns the cached NMI line level.
    if (group == "irq" || group == "nmi" || group == "all")
    {
        out << "\nCIA2 NMI Output\n\n";
        out << "NMI asserted = "
            << (nmiAsserted ? "Yes" : "No")
            << "\n";
    }

    return out.str();
}

void CIA2::recomputeIEC()
{
    if (!bus)
        return;

    auto released = [&](uint8_t mask) -> bool
    {
        // Input means CIA is not driving the line.
        if (!isPortAOutput(mask))
            return true;

        // CIA2 IEC output bits are inverted:
        // latch 0 = released/high
        // latch 1 = pulled low/asserted
        return !getPortALatchBit(mask);
    };

    const bool atnReleased  = released(MASK_ATN_OUT);
    const bool clkReleased  = released(MASK_CLK_OUT);
    const bool dataReleased = released(MASK_DATA_OUT);

    bus->setC64IECOutputs(atnReleased, clkReleased, dataReleased);

    TraceManager* traceMgr = getTraceManager();
    if (traceMgr && traceMgr->ciaDetailOn(2, TraceManager::TraceDetail::CIA_IEC))
    {
        std::ostringstream out;
        out << "[CIA2:IEC] DRIVE "
            << "ATN=" << (atnReleased  ? "H" : "L") << " "
            << "CLK=" << (clkReleased  ? "H" : "L") << " "
            << "DATA=" << (dataReleased ? "H" : "L")
            << " PRA=$" << std::hex << std::uppercase
            << std::setw(2) << std::setfill('0') << int(getPortALatch())
            << " DDRA=$" << std::setw(2) << int(getPortADDR());

        traceMgr->recordCustomEvent(out.str(), makeCIAStamp());
    }
}

void CIA2::updateRS232Outputs()
{
    if (!rs232dev)
        return;

    // PA2 = RS232 TXD
    if (isPortAOutput(TXD_MASK))
        rs232dev->setTXD(getPortALatchBit(TXD_MASK));

    // PB1 = RTS
    if (isPortBOutput(RTS_MASK))
        rs232dev->setRTS(getPortBLatchBit(RTS_MASK));

    // PB2 = DTR
    if (isPortBOutput(DTR_MASK))
        rs232dev->setDTR(getPortBLatchBit(DTR_MASK));
}

TraceManager::Stamp CIA2::makeCIAStamp() const
{
    TraceManager* traceMgr = getTraceManager();

    if (!traceMgr)
        return { 0, 0, 0 };

    return traceMgr->makeStamp(
        cpu ? cpu->getTotalCycles() : 0,
        vic ? vic->getCurrentRaster() : 0,
        vic ? vic->getRasterDot() : 0);
}

CIA2::IECSnapshot CIA2::snapshotIEC() const
{
    IECSnapshot s{};

    s.pra  = getPortALatch();
    s.ddra = getPortADDR();

    auto released = [&](uint8_t mask) -> bool
    {
        if (!isPortAOutput(mask))
            return true;              // input = released

        return !getPortALatchBit(mask); // output 0 = released, output 1 = pull low
    };

    s.atnOutReleased  = released(MASK_ATN_OUT);
    s.clkOutReleased  = released(MASK_CLK_OUT);
    s.dataOutReleased = released(MASK_DATA_OUT);

    if (bus)
    {
        s.clkInHigh  = bus->readClkLine();
        s.dataInHigh = bus->readDataLine();
        s.srqInHigh  = bus->readSrqLine();
    }

    s.legacyProtocolEnabled  = iecProtocolEnabled;
    s.legacyListening        = listening;
    s.legacyTalking          = talking;
    s.legacySecondaryAddress = currentSecondaryAddress;

    return s;
}

std::string CIA2::debugIECSnapshotString() const
{
    const auto s = snapshotIEC();

    std::ostringstream out;

    out << "C64 CIA2 IEC:\n";
    out << "  PRA=$"
        << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << int(s.pra)
        << "  DDRA=$"
        << std::setw(2) << int(s.ddra)
        << std::dec << "\n";

    out << "  Outputs: "
        << "ATN="  << (s.atnOutReleased  ? "released" : "pull-low") << "  "
        << "CLK="  << (s.clkOutReleased  ? "released" : "pull-low") << "  "
        << "DATA=" << (s.dataOutReleased ? "released" : "pull-low") << "\n";

    out << "  Inputs: "
        << "CLK="  << (s.clkInHigh  ? "H" : "L") << "  "
        << "DATA=" << (s.dataInHigh ? "H" : "L") << "  "
        << "SRQ="  << (s.srqInHigh  ? "H" : "L") << "\n";

    out << "  Legacy/software decode: "
        << "enabled=" << (s.legacyProtocolEnabled ? "yes" : "no")
        << " listening=" << (s.legacyListening ? "yes" : "no")
        << " talking=" << (s.legacyTalking ? "yes" : "no")
        << " sa=$"
        << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
        << int(s.legacySecondaryAddress)
        << std::dec << "\n";

    return out.str();
}
