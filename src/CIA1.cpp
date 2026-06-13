// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "cia1.h"

CIA1::CIA1() :
    cass(nullptr),
    cpu(nullptr),
    IRQ(nullptr),
    joy1(nullptr),
    joy2(nullptr),
    keyb(nullptr),
    logger(nullptr),
    mem(nullptr),
    vic(nullptr)
{
    setMode(VideoMode::NTSC);
}

CIA1::~CIA1() = default;

void CIA1::attachJoystickInstance(Joystick* joy)
{
    if (!joy)
    {
        return;
    }

    if (joy->getPort() == 1)
    {
        this->joy1 = joy;
    }
    else
    {
        this->joy2 = joy;
    }
}

void CIA1::detachJoystickInstance(Joystick* joy)
{
    if (joy == joy1)
    {
        joy1 = nullptr;
    }
    else if (joy == joy2)
    {
        joy2 = nullptr;
    }
    else
    {
        throw std::runtime_error("ERROR: No such Joystick!");
    }
}

void CIA1::saveState(StateWriter& wrtr) const
{
    // CIA1 = "Core" and Registers
    wrtr.beginChunk("CIA1");
    wrtr.writeU32(1); // version

    // Base save
    saveBaseState(wrtr);

    // CIA1-specific state
    wrtr.writeU8(portAValue);
    wrtr.writeU8(rowState);
    wrtr.writeU8(activeRow);
    wrtr.writeI32(rowIndex);

    // End the chunk for CIA1
    wrtr.endChunk();

    // Write CI1X chunk for runtime statue
    wrtr.beginChunk("CI1X");
    wrtr.writeU32(1); // version

    // Base runtime state
    saveBaseRuntimeState(wrtr);

    // Dump current cassette state
    wrtr.writeBool(prevReadLevel);
    wrtr.writeBool(cassetteReadLineLevel);
    wrtr.writeBool(gateWasOpenPrev);

    // End the chunk
    wrtr.endChunk();
}

bool CIA1::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "CIA1", 4) == 0)
    {
        rdr.enterChunkPayload(chunk);

        uint32_t ver = 0;
        if (!rdr.readU32(ver))                                          { rdr.exitChunkPayload(chunk); return false; }
        if (ver != 1)                                                   { rdr.exitChunkPayload(chunk); return false; }

        // Load base state
        if (!loadBaseState(rdr))                                        { rdr.exitChunkPayload(chunk); return false; }

        // CIA1-specific state
        if (!rdr.readU8(portAValue))                                    { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU8(rowState))                                      { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readU8(activeRow))                                     { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readI32(rowIndex))                                     { rdr.exitChunkPayload(chunk); return false; }

        // End chunk
        rdr.exitChunkPayload(chunk);
        return true;
    }

    if (std::memcmp(chunk.tag, "CI1X", 4) == 0)
    {
        rdr.enterChunkPayload(chunk);

        uint32_t ver = 0;
        if (!rdr.readU32(ver))                                          { rdr.exitChunkPayload(chunk); return false; }
        if (ver != 1)                                                   { rdr.exitChunkPayload(chunk); return false; }

        if (!loadBaseRuntimeState(rdr))                                 { rdr.exitChunkPayload(chunk); return false; }

        // Load current cassette state
        if (!rdr.readBool(prevReadLevel))                               { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(cassetteReadLineLevel))                       { rdr.exitChunkPayload(chunk); return false; }
        if (!rdr.readBool(gateWasOpenPrev))                             { rdr.exitChunkPayload(chunk); return false; }

        rdr.exitChunkPayload(chunk);

        postLoadState();

        return true;
    }

    // Unknown chunk
    return false;
}

void CIA1::reset()
{
    CIA6526::reset();

    portAValue = 0;
    rowState = 0xFF;  // inputs float high
    activeRow = 0;
    rowIndex = 0;

    // Update the cassette
    prevReadLevel = true;
    cassetteReadLineLevel = true;
    gateWasOpenPrev = false;

    if (cass && cass->isCassetteLoaded())
        prevReadLevel = cass->getData(); // sample actual line level
    else
        prevReadLevel = true; // default pulled-up

    // Clear all IRQ's
    if (IRQ) IRQ->clearIRQ(IRQLine::CIA1);
}

uint8_t CIA1::readPortA()
{
    uint8_t pin = 0xFF;  // pulled-up bus

    // Keyboard rows selected by PRB (only when a column is driven low)
    if (keyb)
    {
        for (int r = 0; r < 8; ++r)
            if ((getPortBOutput() & (1u << r)) == 0)
                pin &= keyb->readRow(r);  // active-low
    }

    // JOY2 (PA0..PA4 active-low). Mask to 5 bits; keep PA5..PA7 high.
    if (joy2)
    {
        uint8_t j2 = static_cast<uint8_t>((joy2->getState() & 0x1F) | 0xE0);
        pin &= j2;
    }

    // Wire-AND cassette SENSE onto PA4 (low dominates)
    const bool senseLow = mem ? mem->getCassetteSenseLow() : false;
    if (senseLow) pin &= static_cast<uint8_t>(~0x10);

    // Merge with output latch via DDRA…
    uint8_t v = static_cast<uint8_t>(getPortAOutput() & pin);

    portAValue = v;
    return v;
}

uint8_t CIA1::readPortB()
{
    uint8_t pa = getPortAOutput();
    // Calculate the active row
    if (pa == 0xFF)
        rowState = 0xFF;
    else
    {
        // Find the active rows so the keyboard keys work
        uint8_t combinedRowState = 0xFF;
        for (uint8_t i = 0; i < 8; i++)
        {
            if ((pa & (1 << i)) == 0 && keyb)
            {
                combinedRowState &= keyb->readRow(i);
            }
        }
        rowState = combinedRowState;
    }

    // Add Joystick 1 state if attached
    if (joy1)
    {
        rowState &= static_cast<uint8_t>((joy1->getState() & 0x1F) | 0xE0);
    }

    // Combine PortB and row state
    return getPortBOutput() & rowState;
}

void CIA1::postTimerUpdates(uint32_t cyclesElapsed)
{
    // Cassette handler (FLAG falling-edge detection)
    for (uint32_t i = 0; i < cyclesElapsed; ++i)
    {
        const bool motorOn  = mem ? mem->isCassetteMotorOn()
                                  : (cass ? cass->motorOn() : false);
        const bool senseLow = mem ? mem->getCassetteSenseLow() : false;
        const bool allow    = motorOn && senseLow;

        if (allow && !gateWasOpenPrev)
        {
            prevReadLevel = true; // prime so the next low becomes a falling edge
        }
        gateWasOpenPrev = allow;

        if (allow && cass)
        {
            // Advance tape by one CPU cycle and sample READ
            cass->tick();
            const bool level = cass->getData(); // true = high (idle), false = low (pulse)
            cassetteReadLineLevel = level;

            if (prevReadLevel && !level)
            {
                triggerInterrupt(INTERRUPT_FLAG_LINE);
            }

            // Remember for next cycle (only when gate open)
            prevReadLevel = level;
        }
        else
        {
            // Gate closed or no device: line is pulled up; DO NOT touch prevReadLevel
            cassetteReadLineLevel = true;
        }
    }
}

void CIA1::irqLineChanged(bool active)
{
    if (!IRQ) return;
    if (active)
        IRQ->raiseIRQ(IRQLine::CIA1);
    else
        IRQ->clearIRQ(IRQLine::CIA1);
}

void CIA1::latchTODClock()
{
    todLatch[0] = todClock[0];
    todLatch[1] = todClock[1];
    todLatch[2] = todClock[2];
    todLatch[3] = todClock[3];
    todLatched = true;
}

void CIA1::incrementTODClock(uint32_t& todTicks, uint8_t todClock[], uint32_t todIncrementThreshold)
{
    todTicks -= todIncrementThreshold;

    // Increment TOD clock
    todClock[0]++; // Increment 1/10 seconds
    if (todClock[0] >= 10)
    {
        todClock[0] = 0;
        todClock[1]++; // Increment seconds
        if (todClock[1] >= 60)
        {
            todClock[1] = 0;
            todClock[2]++; // Increment minutes
            if (todClock[2] >= 60)
            {
                todClock[2] = 0;
                todClock[3]++; // Increment hours
                if (todClock[3] >= 24)
                {
                    todClock[3] = 0; // Wrap around hours
                }
            }
        }
    }
}

void CIA1::checkTODAlarm(uint8_t todClock[], const uint8_t todAlarm[], bool& todAlarmTriggered, uint8_t& interruptStatus, uint8_t interruptEnable)
{
    if (todClock[0] == todAlarm[0] && todClock[1] == todAlarm[1] &&
        todClock[2] == todAlarm[2] && todClock[3] == todAlarm[3])
    {
        if (!todAlarmTriggered)
        {
            todAlarmTriggered = true;
            triggerInterrupt(INTERRUPT_TOD_ALARM);

            TraceManager* traceMgr = getTraceManager();
            if (traceMgr && traceMgr->isEnabled() && traceMgr->catOn(TraceManager::TraceCat::CIA1))
            {
                traceMgr->recordCiaICR(1, interruptStatus, (interruptStatus & interruptEnable & 0x1F) != 0, makeCIAStamp());
            }
        }
    }
}

/*void CIA1::triggerInterrupt(InterruptBit interruptBit)
{
    interruptStatus |= interruptBit; // Set the relevant bit in the status register

    TraceManager* traceMgr = getTraceManager();
    if (traceMgr && traceMgr->isEnabled() && traceMgr->catOn(TraceManager::TraceCat::CIA1))
    {
        traceMgr->recordCiaICR(1, interruptStatus, (interruptStatus & interruptEnable & 0x1F) != 0, makeCIAStamp());
    }

    refreshMasterBit();
    updateIRQLine();
}

void CIA1::updateIRQLine()
{
    if (!IRQ) return;
    const bool any_pending = (interruptStatus & interruptEnable & 0x1F) != 0;
    if (any_pending)
        IRQ->raiseIRQ(IRQLine::CIA1);
    else
        IRQ->clearIRQ(IRQLine::CIA1);
}

void CIA1::clearInterrupt(InterruptBit interruptBit)
{
    clearIFR(interruptBit);
}

void CIA1::clearIFR(InterruptBit interruptBit)
{
    interruptStatus &= ~interruptBit;

    TraceManager* traceMgr = getTraceManager();
    if (traceMgr && traceMgr->isEnabled() && traceMgr->catOn(TraceManager::TraceCat::CIA1))
    {
        traceMgr->recordCiaICR(1, interruptStatus, (interruptStatus & interruptEnable & 0x1F) != 0, makeCIAStamp());
    }

    refreshMasterBit();
    updateIRQLine();
}

void CIA1::refreshMasterBit()
{
    if ((interruptStatus & 0x1F) != 0) interruptStatus |= 0x80;
    else interruptStatus &= ~0x80;

    TraceManager* traceMgr = getTraceManager();
    if (traceMgr && traceMgr->isEnabled() && traceMgr->catOn(TraceManager::TraceCat::CIA1))
    {
        traceMgr->recordCiaICR(1, interruptStatus, (interruptStatus & interruptEnable & 0x1F) != 0, makeCIAStamp());
    }
}*/

std::string CIA1::dumpRegisters(const std::string& group) const
{
    std::stringstream out;

    // First dump the generic 6526 registers from the base class.
    out << CIA6526::dumpRegisters(group);

    // Then append CIA1/C64-specific details.
    if (group == "port" || group == "all")
    {
        const bool senseLow  = mem ? mem->getCassetteSenseLow() : false;
        const bool motorOn   = mem ? mem->isCassetteMotorOn() : false;
        const bool readLevel = cassetteReadLineLevel;

        out << "\nCIA1 C64-Specific Lines\n\n";

        out << "Cassette SENSE / PA4       = "
            << (senseLow ? "Low" : "High")
            << "  (CIA sees bit as "
            << (senseLow ? "0" : "1") << ")\n";

        out << "Cassette READ line         = "
            << (readLevel ? "High" : "Low") << "\n";

        out << "6510 $0001 motor           = "
            << (motorOn ? "ON" : "OFF")
            << "  (bit5 effective = "
            << (motorOn ? "0" : "1") << ")\n";
    }

    return out.str();
}

TraceManager::Stamp CIA1::makeCIAStamp() const
{
    TraceManager* traceMgr = getTraceManager();

    return traceMgr->makeStamp(
        cpu ? cpu->getTotalCycles() : 0,
        vic ? vic->getCurrentRaster() : 0,
        vic ? vic->getRasterDot() : 0);
}
