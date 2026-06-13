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

    // ML Monitor logging disable by default
    setLogging = false;
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

/*void CIA1::updateTimers(uint32_t cyclesElapsed)
{
    // Handle Timer A / B as before
    updateTimerA(cyclesElapsed);
    updateTimerB(cyclesElapsed);

    // TOD
    todTicks += cyclesElapsed;
    while (todTicks >= todIncrementThreshold)
    {
        incrementTODClock(todTicks, todClock, todIncrementThreshold);
    }
    checkTODAlarm(todClock, todAlarm, todAlarmTriggered, interruptStatus, interruptEnable);

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

    // Reflect master bit and shared IRQ line
    refreshMasterBit();
    updateIRQLine();
}*/

void CIA1::cntChangedA()
{
    if (inputMode == InputMode::modeCNT && (timerAControl & 0x01))
    {
        uint32_t current = timerA ? timerA : 0x10000;
        if (--current == 0)
        {
            // --- Underflow semantics (same as phi2 path) ---
            triggerInterrupt(INTERRUPT_TIMER_A);

            TraceManager* traceMgr = getTraceManager();
            if (traceMgr && traceMgr->isEnabled() && traceMgr->catOn(TraceManager::TraceCat::CIA1))
            {
                traceMgr->recordCiaTimer(1, 'A', timerA, true, makeCIAStamp());
            }

            // Serial shift on TA underflow if enabled (CRA bit6)
            if (timerAControl & 0x40)
            {
                const uint8_t bit = cntLevel ? 1u : 0u;     // sample CNT level
                shiftReg = static_cast<uint8_t>((shiftReg << 1) | (bit & 1));
                if (++shiftCount == 8)
                {
                    serialDataRegister = shiftReg;
                    shiftCount = 0;
                    triggerInterrupt(INTERRUPT_SERIAL_SHIFT_REGISTER);
                }
            }

            // Cascade TB if CRB bit6 set
            if (timerBControl & 0x40)
            {
                handleTimerBCascade();
            }

            // Reload / one-shot stop
            const bool continuous = !(timerAControl & 0x08);
            timerA = continuous ? (timerAHighByte << 8) | timerALowByte : 0;
            if (!continuous)
            {
                timerAControl &= ~0x01; // stop on one-shot
            }
        }
        else
        {
            timerA = static_cast<uint16_t>(current);
        }
    }
}

void CIA1::cntChangedB()
{
    const bool tbStarted = (timerBControl & 0x01);
    const bool tbCNT     = (timerBControl & 0x20); // CNT source
    const bool tbCascade = (timerBControl & 0x40); // cascade source

    // TB counts on CNT only if CRB selects CNT and not cascade
    if (!tbStarted || !tbCNT || tbCascade) return;

    uint32_t current = timerB ? timerB : 0x10000;
    if (--current == 0)
    {
        triggerInterrupt(INTERRUPT_TIMER_B);

        TraceManager* traceMgr = getTraceManager();
        if (traceMgr && traceMgr->isEnabled() && traceMgr->catOn(TraceManager::TraceCat::CIA1))
        {
            traceMgr->recordCiaTimer(1, 'B', timerB, true, makeCIAStamp());
        }

        const bool continuous = !(timerBControl & 0x08);
        if (continuous)
        {
            timerB = (timerBHighByte << 8) | timerBLowByte;
        }
        else
        {
            timerB = 0;
            timerBControl &= ~0x01; // stop in one-shot
        }
    }
    else
    {
        timerB = static_cast<uint16_t>(current);
    }
}

void CIA1::updateTimerA(uint32_t cyclesElapsed)
{
    if (!(timerAControl & 0x01) || cyclesElapsed == 0) return;

    const bool taCntMode = (timerAControl & 0x20) != 0;  // bit5: 1=CNT, 0=φ2
    if (taCntMode) return;  // CNT-driven, handled elsewhere

    while (cyclesElapsed > 0)
    {
        uint32_t cur = (timerA == 0) ? 0x10000u : timerA;  // promote to 32-bit

        if (cyclesElapsed < cur)
        {
            timerA = static_cast<uint16_t>(cur - cyclesElapsed);
            break;
        }

        // Consume until underflow
        cyclesElapsed -= cur;
        timerA = 0;

        // Underflow: latch IRQ
        triggerInterrupt(INTERRUPT_TIMER_A);

        TraceManager* traceMgr = getTraceManager();
        if (traceMgr && traceMgr->isEnabled() && traceMgr->catOn(TraceManager::TraceCat::CIA1))
        {
            traceMgr->recordCiaTimer(1, 'A', timerA, true, makeCIAStamp());
        }

        const bool continuous = !(timerAControl & 0x08); // RUNMODE=0 => continuous
        if (continuous)
        {
            timerA = (timerAHighByte << 8) | timerALowByte;
        }
        else
        {
            timerA = 0;
            timerAControl &= ~0x01; // stop in one-shot
            break;
        }

        // Cascade TB if CRB bit6 set
        if (timerBControl & 0x40)
        {
            handleTimerBCascade();
        }

        // Serial shift register (CRA bit6)
        if (timerAControl & 0x40)
        {
            const uint8_t bit = cntLevel ? 1u : 0u;
            shiftReg = static_cast<uint8_t>((shiftReg << 1) | (bit & 1));
            if (++shiftCount == 8)
            {
                serialDataRegister = shiftReg;
                shiftCount = 0;
                triggerInterrupt(INTERRUPT_SERIAL_SHIFT_REGISTER);
            }
        }
    }
}

void CIA1::updateTimerB(uint32_t cyclesElapsed)
{
    const bool tbStarted = (timerBControl & 0x01);
    const bool tbCNT     = (timerBControl & 0x20); // CNT vs φ2
    const bool tbCascade = (timerBControl & 0x40); // cascade from TA

    if (!tbStarted || tbCNT || tbCascade || cyclesElapsed == 0) return;

    while (cyclesElapsed > 0)
    {
        uint32_t cur = (timerB == 0) ? 0x10000u : timerB;

        if (cyclesElapsed < cur)
        {
            timerB = static_cast<uint16_t>(cur - cyclesElapsed);
            break;
        }

        // Consume until underflow
        cyclesElapsed -= cur;
        timerB = 0;

        // Underflow: latch IRQ
        triggerInterrupt(INTERRUPT_TIMER_B);

        TraceManager* traceMgr = getTraceManager();
        if (traceMgr && traceMgr->isEnabled() && traceMgr->catOn(TraceManager::TraceCat::CIA1))
        {
            traceMgr->recordCiaTimer(1, 'B', timerB, true, makeCIAStamp());
        }

        const bool continuous = !(timerBControl & 0x08);
        if (continuous)
        {
            timerB = (timerBHighByte << 8) | timerBLowByte;
        }
        else
        {
            timerB = 0;
            timerBControl &= ~0x01; // stop in one-shot
            break;
        }
    }
}

void CIA1::handleTimerBCascade()
{
    if (!(timerBControl & 0x40) || !(timerBControl & 0x01))
        return;

    // one “tick” of B
    uint32_t current = timerB ? timerB : 0x10000;
    if (current > 1)
    {
        timerB = static_cast<uint16_t>(current - 1);
    }
    else
    {
        // underflow
        triggerInterrupt(INTERRUPT_TIMER_B);

        TraceManager* traceMgr = getTraceManager();
        if (traceMgr && traceMgr->isEnabled() && traceMgr->catOn(TraceManager::TraceCat::CIA1))
        {
            traceMgr->recordCiaTimer(1, 'B', timerB, true, makeCIAStamp());
        }

        bool continuous = !(timerBControl & 0x08);
        if (continuous)
            timerB = (timerBHighByte << 8) | timerBLowByte;
        else
        {
            timerB = 0;
            timerBControl &= ~0x01;  // stop
        }
    }
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

void CIA1::setCNTLine(bool level)
{
    const bool falling = (lastCNT && !level);

    TraceManager* traceMgr = getTraceManager();
    if (traceMgr && traceMgr->ciaDetailOn(1, TraceManager::TraceDetail::CIA_CNT))
    {
        std::ostringstream out;
        out << "[CIA1:CNT] level=" << (level ? "H" : "L")
            << " falling=" << (falling ? "Y" : "N");
        traceMgr->recordCustomEvent(out.str(), makeCIAStamp());
    }

    lastCNT = cntLevel = level;
    if (!falling) return;

    if ((timerAControl & 0x20) && (timerAControl & 0x01))
        cntChangedA();

    if ((timerBControl & 0x20) && (timerBControl & 0x01) && !(timerBControl & 0x40))
        cntChangedB();
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
    out << std::hex << std::uppercase << std::setfill('0');

    auto hex2 = [&](uint8_t v){ std::ostringstream s; s<<std::hex<<std::uppercase<<std::setfill('0')<<std::setw(2)<<int(v); return s.str(); };
    auto hex4 = [&](uint16_t v){ std::ostringstream s; s<<std::hex<<std::uppercase<<std::setfill('0')<<std::setw(4)<<v; return s.str(); };

    // Compute effective port values (pull-ups on inputs)
    uint8_t invA = static_cast<uint8_t>(~dataDirectionPortA);
    uint8_t invB = static_cast<uint8_t>(~dataDirectionPortB);
    uint8_t effA = static_cast<uint8_t>((portA & dataDirectionPortA) | invA);
    uint8_t effB = static_cast<uint8_t>((portB & dataDirectionPortB) | invB);

    // Derive cassette bits
    bool senseLow =  mem ? mem->getCassetteSenseLow() : false;
    bool motorOn = mem ? mem->isCassetteMotorOn() : false;
    bool readLevel = cassetteReadLineLevel;

    // Ports
    if (group == "port" || group == "all")
    {
        out << "\nPort Registers \n\n";
        out << "PORT A (latch)             = $" << hex2(portA) << "\n";
        out << "PORT A DDR                 = $" << hex2(dataDirectionPortA) << "\n";
        if ((dataDirectionPortA & 0x10) == 0)
        {
            out << "PORT A (effective)         = $" << hex2(effA)
                << "   [PA4 SENSE:" << (senseLow ? "0" : "1") << "]\n";
        }
        else
        {
            out << "PORT A (effective)         = $" << hex2(effA)
                << "   [PA4 output bit:" << (((portA >> 4) & 1) ? "1" : "0") << "]\n";
        }

        out << "PORT B (latch)             = $" << hex2(portB) << "\n";
        out << "PORT B DDR                 = $" << hex2(dataDirectionPortB) << "\n";
        out << "PORT B (effective)         = $" << hex2(effB) << "\n";
        out << "6510 $0001 motor           = " << (motorOn ? "ON" : "OFF") << "  (bit5 effective = "
            << (motorOn ? "0" : "1") << ")\n";
        out << "Cassette READ line         = " << (readLevel ? "High" : "Low") << "\n";
    }

    // Timers
    if (group == "timer" || group == "all")
    {
        auto decodeCRA = [&](uint8_t cr)
        {
            std::ostringstream s;
            s << "Start:" << ((cr&0x01)?"On":"Off")
              << " PBON:" << ((cr&0x02)?"Yes":"No")
              << " OUTMODE:" << ((cr&0x04)?"Toggle":"Pulse")
              << " Mode:" << ((cr&0x08)?"One-shot":"Continuous")
              << " Load:" << ((cr&0x10)?"Yes":"No")
              << " Clock:" << ((cr&0x20)?"CNT":"φ2");
            return s.str();
        };
        auto decodeCRB = [&](uint8_t cr)
        {
            std::ostringstream s;
            s << "Start:" << ((cr&0x01)?"On":"Off")
              << " PBON:" << ((cr&0x02)?"Yes":"No")
              << " OUTMODE:" << ((cr&0x04)?"Toggle":"Pulse")
              << " Mode:" << ((cr&0x08)?"One-shot":"Continuous")
              << " Load:" << ((cr&0x10)?"Yes":"No")
              << " Clock:" << ((cr&0x60)==0x00?"φ2": (cr&0x60)==0x20?"CNT": (cr&0x60)==0x40?"TimerA":"TimerA+CNT");
            return s.str();
        };

        out << "\nTimer Registers \n\n";
        out << "Timer A Latch Low          = $" << hex2(timerALowByte)  << "\n";
        out << "Timer A Latch High         = $" << hex2(timerAHighByte) << "\n";
        out << "Timer A Latched            = " << (timerALatched ? "Yes" : "No")
            << "  Snapshot = $" << hex4(timerASnap) << " (High will return snapshot: "
            << (timerALatched ? "Yes" : "No") << ")\n";
        out << "Timer A Current            = $" << hex4(timerA) << "\n";
        out << "Timer A Control Register   = $" << hex2(timerAControl) << "  [" << decodeCRA(timerAControl) << "]\n";

        out << "Timer B Latch Low          = $" << hex2(timerBLowByte)  << "\n";
        out << "Timer B Latch High         = $" << hex2(timerBHighByte) << "\n";
        out << "Timer B Latched            = " << (timerBLatched ? "Yes" : "No")
            << "  Snapshot = $" << hex4(timerBSnap) << " (High will return snapshot: "
            << (timerBLatched ? "Yes" : "No") << ")\n";
        out << "Timer B Current            = $" << hex4(timerB) << "\n";
        out << "Timer B Control Register   = $" << hex2(timerBControl) << "  [" << decodeCRB(timerBControl) << "]\n";
    }

    // TOD
    if (group == "tod" || group == "all")
    {
        out << "\nTOD Registers\n\n";
        out << "Current TOD                = "
            << hex2(todClock[3]) << ":" << hex2(todClock[2]) << ":" << hex2(todClock[1]) << "." << hex2(todClock[0]) << "\n";
        out << "TOD Alarm                  = "
            << hex2(todAlarm[3]) << ":" << hex2(todAlarm[2]) << ":" << hex2(todAlarm[1]) << "." << hex2(todAlarm[0]) << "\n";
        out << "TOD Latch                  = "
            << hex2(todLatch[3]) << ":" << hex2(todLatch[2]) << ":" << hex2(todLatch[1]) << "." << hex2(todLatch[0]) << "\n";
        out << "TOD Alarm Set Mode         = " << (todAlarmSetMode ? "Yes" : "No") << "\n";
    }

    // Interrupts
    if (group == "icr" || group == "all")
    {
        uint8_t sources = static_cast<uint8_t>(interruptStatus & 0x1F);
        bool masterPending = (sources & interruptEnable) != 0;
        out << "\nInterrupt Registers\n\n";
        out << "Interrupt Status (IFR)     = $" << hex2(interruptStatus) << "  [";
        if (interruptStatus & INTERRUPT_TIMER_A) out << " TA";
        if (interruptStatus & INTERRUPT_TIMER_B) out << " TB";
        if (interruptStatus & INTERRUPT_TOD_ALARM) out << " TOD";
        if (interruptStatus & INTERRUPT_SERIAL_SHIFT_REGISTER) out << " SR";
        if (interruptStatus & INTERRUPT_FLAG_LINE) out << " FLAG";
        out << " ]  Master Pending: " << (masterPending ? "Yes" : "No") << "\n";
        out << "Interrupt Enable (IER)     = $" << hex2(interruptEnable) << "\n";
    }

    // Serial
    if (group == "serial" || group == "all")
    {
        out << "\nSerial Register\n\n";
        out << "Serial Data Register       = $" << hex2(serialDataRegister) << "\n";
    }

    // Mode
    if (group == "mode" || group == "all")
    {
        out << "\nControl Lines\n\n";
        out << "CNT Input Mode             = ";
        switch (inputMode)
        {
            case InputMode::modeProcessor: out << "Processor polling"; break;
            case InputMode::modeCNT:       out << "CNT-driven"; break;
            case InputMode::modeTimerA:    out << "Timer A-driven"; break;
            case InputMode::modeTimerACNT: out << "Timer A + CNT"; break;
        }
        out << "\nPrevious FLAG state        = " << (prevReadLevel ? "High" : "Low") << "\n";
    }

    return out.str();
}

void CIA1::setIERExact(uint8_t mask)
{
    mask &= 0x1F;
    interruptEnable = mask;
    //*updateIRQLine();
}

TraceManager::Stamp CIA1::makeCIAStamp() const
{
    TraceManager* traceMgr = getTraceManager();

    return traceMgr->makeStamp(
        cpu ? cpu->getTotalCycles() : 0,
        vic ? vic->getCurrentRaster() : 0,
        vic ? vic->getRasterDot() : 0);
}
