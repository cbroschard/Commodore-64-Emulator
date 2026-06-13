// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "CIA6526.h"

CIA6526::CIA6526() :
    portA(0xFF),
    portB(0xFF),
    ddrA(0x00),
    ddrB(0x00),
    timerA(0),
    timerASnap(0),
    timerALatched(false),
    timerB(0),
    timerBSnap(0),
    timerBLatched(false),
    timerALowByte(0),
    timerAHighByte(0),
    timerBLowByte(0),
    timerBHighByte(0),
    timerAControl(0),
    timerBControl(0),
    interruptStatus(0),
    interruptEnable(0),
    serialDataRegister(0),
    todTicks(0),
    todLatched(false),
    todAlarmSetMode(false),
    todAlarmTriggered(false),
    cntLevel(true),
    lastCNT(true),
    shiftReg(0),
    shiftCount(0),
    setLogging(false)
{
    setMode(VideoMode::NTSC);
}

CIA6526::~CIA6526() = default;

void CIA6526::reset()
{
    portA               = 0xFF;
    portB               = 0xFF;
    ddrA                = 0x00;
    ddrB                = 0x00;

    timerA              = 0;
    timerASnap          = 0;
    timerALatched       = false;

    timerB              = 0;
    timerBSnap          = 0;
    timerBLatched       = false;

    timerALowByte       = 0;
    timerAHighByte      = 0;
    timerBLowByte       = 0;
    timerBHighByte      = 0;

    timerAControl       = 0;
    timerBControl       = 0;

    interruptStatus     = 0;
    interruptEnable     = 0;

    serialDataRegister  = 0;

    todClock[0]         = 0;
    todClock[1]         = 0;
    todClock[2]         = 0;
    todClock[3]         = 0;

    todAlarm[0]         = 0;
    todAlarm[1]         = 0;
    todAlarm[2]         = 0;
    todAlarm[3]         = 0;

    todLatch[0]         = 0;
    todLatch[1]         = 0;
    todLatch[2]         = 0;
    todLatch[3]         = 0;

    todTicks            = 0;

    todLatched          = false;
    todAlarmSetMode     = false;
    todAlarmTriggered   = false;

    cntLevel            = true;
    lastCNT             = true;

    shiftReg            = 0;
    shiftCount          = 0;

    setLogging          = false;
}

void CIA6526::setMode(VideoMode mode)
{
    mode_ = mode;
    todIncrementThreshold = (mode_ == VideoMode::NTSC) ? 102273 : 98525;
}

void CIA6526::setCNTLine(bool level)
{
    const bool falling = cntLevel && !level;

    TraceManager* traceMgr = getTraceManager();
    if (traceMgr && traceMgr->ciaDetailOn(getCIANumber(), TraceManager::TraceDetail::CIA_CNT))
    {
        std::ostringstream out;
        out << "[" << getCIAName() << ":CNT] level="
            << (level ? "H" : "L")
            << " falling="
            << (falling ? "Y" : "N");

        traceMgr->recordCustomEvent(out.str(), makeCIAStamp());
    }

    lastCNT = cntLevel;
    cntLevel = level;

    if (!falling)
        return;

    cntChangedA();
    cntChangedB();
}

void CIA6526::updateTimers(uint32_t cyclesElapsed)
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

    // Run chip specific processing
    postTimerUpdates(cyclesElapsed);

    // Reflect master bit and shared IRQ line
    refreshMasterBit();
    updateIRQLine();
}

uint8_t CIA6526::readRegister(uint16_t address)
{
    uint8_t reg = address & 0x0F;

    switch (reg)
    {
        case 0x00:
            return readPortA();
        case 0x01:
            return readPortB();
        case 0x02:
            return ddrA;
        case 0x03:
            return ddrB;
        case 0x04:
        {
            timerASnap = timerA;
            timerALatched = true;
            return static_cast<uint8_t>(timerASnap & 0xFF);
        }
        case 0x05:
        {
            if (timerALatched)
            {
                timerALatched = false;
                return static_cast<uint8_t>((timerASnap >> 8) & 0xFF);
            }
            return static_cast<uint8_t>((timerA >> 8) & 0xFF);
        }
        case 0x06:
        {
            timerBSnap = timerB;
            timerBLatched = true;
            return static_cast<uint8_t>(timerBSnap & 0xFF);
        }
        case 0x07:
        {
            if (timerBLatched)
            {
                timerBLatched = false;
                return static_cast<uint8_t>((timerBSnap >> 8) & 0xFF);
            }
            return static_cast<uint8_t>((timerB >> 8) & 0xFF);
        }
        case 0x08:
        {
            if (!todLatched)
                latchTODClock();
            return binaryToBCD(todLatch[0]);
        }
        case 0x09:
        {
            if (!todLatched)
                latchTODClock();
            return binaryToBCD(todLatch[1]);
        }
        case 0x0A:
        {
            if (!todLatched)
                latchTODClock();
            return binaryToBCD(todLatch[2]);
        }
        case 0x0B:
        {
            if (!todLatched)
                latchTODClock();

            todLatched = false;
            return binaryToBCD(todLatch[3]);
        }
        case 0x0C:
            return serialDataRegister;
        case 0x0D:
        {
            uint8_t result = interruptStatus & 0x1F; // latch bits 0-4 only

            // If any enabled source is active, set bit 7 in the *returned value*
            if (result & interruptEnable) result |= 0x80;

            // Clear the acknowledged sources (bits 0-4 that were set)
            interruptStatus &= ~ (result & 0x1F);

            // If TOD was included, unlock future matches
            if (result & INTERRUPT_TOD_ALARM)
            {
                todAlarmTriggered = false;
            }

            // Recompute master bit and line state
            refreshMasterBit();
            updateIRQLine();

            return result;
        }
        case 0x0E:
            return timerAControl & 0x7F;
        case 0x0F:
            return timerBControl & 0x7F;
        default:
            return 0xFF;
    }

    // Default
    return 0xFF;
}

void CIA6526::writeRegister(uint16_t address, uint8_t value)
{
    uint8_t reg = address & 0x0F;

    switch(reg)
    {
        case 0x00:
        {
            portA = value;
            portAOutputChanged(getPortAOutput());
            break;
        }
        case 0x01:
        {
            portB = value;
            portBOutputChanged(getPortBOutput());
            break;
        }
        case 0x02:
        {
            ddrA = value;
            portAOutputChanged(getPortAOutput());
            break;
        }
        case 0x03:
        {
            ddrB = value;
            portBOutputChanged(getPortBOutput());
            break;
        }
        case 0x04:
        {
            timerALowByte = value;

            if (traceMgr && traceMgr->ciaDetailOn(getCIANumber(), TraceManager::TraceDetail::CIA_TIMER))
            {
                std::ostringstream out;
                out << "[" << getCIAName() << ":TIMER] TA latch low write=$"
                    << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << int(value)
                    << " latch=$"
                    << std::setw(4) << int((uint16_t(timerAHighByte) << 8) | timerALowByte);

                traceMgr->recordCustomEvent(out.str(), makeCIAStamp());
            }

            break;
        }
        case 0x05:
        {
            timerAHighByte = value;

            if (traceMgr && traceMgr->ciaDetailOn(getCIANumber(), TraceManager::TraceDetail::CIA_TIMER))
            {
                std::ostringstream out;
                out << "[" << getCIAName() << ":TIMER] TA latch high write=$"
                    << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << int(value)
                    << " latch=$"
                    << std::setw(4) << int((uint16_t(timerAHighByte) << 8) | timerALowByte);
                traceMgr->recordCustomEvent(out.str(), makeCIAStamp());
            }
            break;
        }
        case 0x06:
        {
            timerBLowByte = value;

            if (traceMgr && traceMgr->ciaDetailOn(getCIANumber(), TraceManager::TraceDetail::CIA_TIMER))
            {
                std::ostringstream out;
                out << "[" << getCIAName() << ":TIMER] TB latch low write=$"
                    << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << int(value)
                    << " latch=$"
                    << std::setw(4) << int((uint16_t(timerBHighByte) << 8) | timerBLowByte);
                traceMgr->recordCustomEvent(out.str(), makeCIAStamp());
            }
            break;
        }
        case 0x07:
        {
            timerBHighByte = value;

            if (traceMgr && traceMgr->ciaDetailOn(getCIANumber(), TraceManager::TraceDetail::CIA_TIMER))
            {
                std::ostringstream out;
                out << "[" << getCIAName() << ":TIMER] TB latch high write=$"
                    << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << int(value)
                    << " latch=$"
                    << std::setw(4) << int((uint16_t(timerBHighByte) << 8) | timerBLowByte);
                traceMgr->recordCustomEvent(out.str(), makeCIAStamp());
            }
            break;
        }
        case 0x08:
        {
            if (todAlarmSetMode) // Writing to alarm
            {
                todAlarm[0] = bcdToBinary(value & 0x0F);
                todAlarmTriggered = false;

                if (traceMgr && traceMgr->ciaDetailOn(getCIANumber(), TraceManager::TraceDetail::CIA_IRQ))
                {
                    std::ostringstream out;
                    out << "[" << getCIAName() << ":TOD] alarm 1/10 write raw=$"
                        << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << int(value)
                        << " alarm="
                        << std::dec << int(todAlarm[3]) << ":"
                        << std::setw(2) << std::setfill('0') << int(todAlarm[2]) << ":"
                        << std::setw(2) << int(todAlarm[1]) << "."
                        << int(todAlarm[0]);
                    traceMgr->recordCustomEvent(out.str(), makeCIAStamp());
                }
            }
            else // Writing to clock
            {
                todClock[0] = bcdToBinary(value & 0x0F);
                if (todLatched)
                    todLatch[0] = todClock[0];

                if (traceMgr && traceMgr->ciaDetailOn(getCIANumber(), TraceManager::TraceDetail::CIA_IRQ))
                {
                    std::ostringstream out;
                    out << "[" << getCIAName() << ":TOD] clock 1/10 write raw=$"
                        << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << int(value)
                        << " clock="
                        << std::dec << int(todClock[3]) << ":"
                        << std::setw(2) << std::setfill('0') << int(todClock[2]) << ":"
                        << std::setw(2) << int(todClock[1]) << "."
                        << int(todClock[0]);
                    traceMgr->recordCustomEvent(out.str(), makeCIAStamp());
                }
            }
            break;
        }
        case 0x09:
        {
            if (todAlarmSetMode)
            {
                todAlarm[1] = bcdToBinary(value & 0x7F);
                todAlarmTriggered = false;

                if (traceMgr && traceMgr->ciaDetailOn(getCIANumber(), TraceManager::TraceDetail::CIA_IRQ))
                {
                    std::ostringstream out;
                    out << "[" << getCIAName() << ":TOD] alarm seconds write raw=$"
                        << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << int(value)
                        << " alarm="
                        << std::dec << int(todAlarm[3]) << ":"
                        << std::setw(2) << std::setfill('0') << int(todAlarm[2]) << ":"
                        << std::setw(2) << int(todAlarm[1]) << "."
                        << int(todAlarm[0]);
                    traceMgr->recordCustomEvent(out.str(), makeCIAStamp());
                }
            }
            else
            {
                todClock[1] = bcdToBinary(value & 0x7F);
                if (todLatched)
                    todLatch[1] = todClock[1];

                if (traceMgr && traceMgr->ciaDetailOn(getCIANumber(), TraceManager::TraceDetail::CIA_IRQ))
                {
                    std::ostringstream out;
                    out << "[" << getCIAName() << ":TOD] clock seconds write raw=$"
                        << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << int(value)
                        << " clock="
                        << std::dec << int(todClock[3]) << ":"
                        << std::setw(2) << std::setfill('0') << int(todClock[2]) << ":"
                        << std::setw(2) << int(todClock[1]) << "."
                        << int(todClock[0]);
                    traceMgr->recordCustomEvent(out.str(), makeCIAStamp());
                }
            }
            break;
        }
        case 0x0A:
        {
            if (todAlarmSetMode)
            {
                todAlarm[2] = bcdToBinary(value & 0x7F);
                todAlarmTriggered = false;

                if (traceMgr && traceMgr->ciaDetailOn(getCIANumber(), TraceManager::TraceDetail::CIA_IRQ))
                {
                    std::ostringstream out;
                    out << "[" << getCIAName() << ":TOD] alarm minutes write raw=$"
                        << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << int(value)
                        << " alarm="
                        << std::dec << int(todAlarm[3]) << ":"
                        << std::setw(2) << std::setfill('0') << int(todAlarm[2]) << ":"
                        << std::setw(2) << int(todAlarm[1]) << "."
                        << int(todAlarm[0]);
                    traceMgr->recordCustomEvent(out.str(), makeCIAStamp());
                }
            }
            else
            {
                todClock[2] = bcdToBinary(value & 0x7F);
                if (todLatched)
                    todLatch[2] = todClock[2];

                if (traceMgr && traceMgr->ciaDetailOn(getCIANumber(), TraceManager::TraceDetail::CIA_IRQ))
                {
                    std::ostringstream out;
                    out << "[" << getCIAName() << ":TOD] clock minutes write raw=$"
                        << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << int(value)
                        << " clock="
                        << std::dec << int(todClock[3]) << ":"
                        << std::setw(2) << std::setfill('0') << int(todClock[2]) << ":"
                        << std::setw(2) << int(todClock[1]) << "."
                        << int(todClock[0]);
                    traceMgr->recordCustomEvent(out.str(), makeCIAStamp());
                }
            }
            break;
        }
        case 0x0B:
        {
            if (todAlarmSetMode)
            {
                todAlarm[3] = bcdToBinary(value & 0x3F);
                todAlarmTriggered = false;

                if (traceMgr && traceMgr->ciaDetailOn(getCIANumber(), TraceManager::TraceDetail::CIA_IRQ))
                {
                    std::ostringstream out;
                    out << "[" << getCIAName() << ":TOD] alarm hours write raw=$"
                        << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << int(value)
                        << " alarm="
                        << std::dec << int(todAlarm[3]) << ":"
                        << std::setw(2) << std::setfill('0') << int(todAlarm[2]) << ":"
                        << std::setw(2) << int(todAlarm[1]) << "."
                        << int(todAlarm[0]);
                    traceMgr->recordCustomEvent(out.str(), makeCIAStamp());
                }
            }
            else
            {
                todClock[3] = bcdToBinary(value & 0x3F);
                if (todLatched)
                    todLatch[3] = todClock[3];

                if (traceMgr && traceMgr->ciaDetailOn(getCIANumber(), TraceManager::TraceDetail::CIA_IRQ))
                {
                    std::ostringstream out;
                    out << "[" << getCIAName() << ":TOD] clock hours write raw=$"
                        << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << int(value)
                        << " clock="
                        << std::dec << int(todClock[3]) << ":"
                        << std::setw(2) << std::setfill('0') << int(todClock[2]) << ":"
                        << std::setw(2) << int(todClock[1]) << "."
                        << int(todClock[0]);
                    traceMgr->recordCustomEvent(out.str(), makeCIAStamp());
                }
            }
            break;
        }
        case 0x0C:
            serialDataRegister = value;
            break;
        case 0x0D:
        {
            uint8_t mask = value & 0x1F;

            if (value & 0x80)
            {
                interruptEnable |= mask;

                if (traceMgr && traceMgr->ciaDetailOn(getCIANumber(), TraceManager::TraceDetail::CIA_IRQ))
                {
                    std::ostringstream out;
                    out << "[" << getCIAName() << ":IRQ] IER write value=$"
                        << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << int(value)
                        << " newIER=$" << std::setw(2) << int(interruptEnable & 0x1F);
                    traceMgr->recordCustomEvent(out.str(), makeCIAStamp());
                }
            }
            else
            {
                interruptEnable &= ~mask;
            }

            refreshMasterBit();
            updateIRQLine();
            break;
        }
        case 0x0E:
        {
            const uint8_t old = timerAControl;
            const uint8_t cra = value & 0x7F;       // ignore bit7

            // Bit4 = LOAD strobe
            if (cra & 0x10)
            {
                timerA = (timerAHighByte << 8) | timerALowByte;
                clearIFR(INTERRUPT_TIMER_A);
            }

            // Store with bit4 cleared (strobe does not latch high)
            timerAControl = static_cast<uint8_t>(cra & ~0x10);

            // Rising edge of START -> reload from latch (6526 behavior)
            const bool wasStarted = (old & 0x01) != 0;
            const bool nowStarted = (timerAControl & 0x01) != 0;
            if (nowStarted && !wasStarted)
            {
                timerA = (timerAHighByte << 8) | timerALowByte;
            }
            if (traceMgr && traceMgr->ciaDetailOn(getCIANumber(), TraceManager::TraceDetail::CIA_TIMER))
            {
                std::ostringstream out;
                out << "[" << getCIAName() << ":TIMER] CRA write=$"
                    << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << int(value)
                    << " latched=$" << std::setw(2) << int(timerAControl)
                    << " timerA=$" << std::setw(4) << timerA;
                traceMgr->recordCustomEvent(out.str(), makeCIAStamp());
            }
            break;
        }
        case 0x0F:
        {
            const uint8_t old = timerBControl;
            todAlarmSetMode = (value & 0x80) != 0;
            const uint8_t crb = value & 0x7F;

            // Bit4 = LOAD strobe
            if (crb & 0x10) {
                timerB = (timerBHighByte << 8) | timerBLowByte;
                clearIFR(INTERRUPT_TIMER_B);
            }

            // Store with bit4 cleared (strobe does not latch high)
            timerBControl = static_cast<uint8_t>(crb & ~0x10);

            // Rising edge of START -> reload from latch
            const bool wasStarted = (old & 0x01) != 0;
            const bool nowStarted = (timerBControl & 0x01) != 0;
            if (nowStarted && !wasStarted)
            {
                timerB = (timerBHighByte << 8) | timerBLowByte;
            }
            if (traceMgr && traceMgr->ciaDetailOn(getCIANumber(), TraceManager::TraceDetail::CIA_TIMER))
            {
                std::ostringstream out;
                out << "[" << getCIAName() << ":TIMER] CRB write=$"
                    << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << int(value)
                    << " latched=$" << std::setw(2) << int(timerBControl)
                    << " timerB=$" << std::setw(4) << timerB;
                traceMgr->recordCustomEvent(out.str(), makeCIAStamp());
            }
            break;
        }
        default:
            break;
    }
}

CIA6526::TimerBClockSource CIA6526::getTimerBClockSource() const
{
    switch (timerBControl & 0x60)
    {
        case 0x00: return TimerBClockSource::Phi2;
        case 0x20: return TimerBClockSource::CNT;
        case 0x40: return TimerBClockSource::TimerA;
        case 0x60: return TimerBClockSource::TimerAWithCNT;
    }

    return TimerBClockSource::Phi2;
}

void CIA6526::updateTimerA(uint32_t cyclesElapsed)
{
    if (!(timerAControl & 0x01) || cyclesElapsed == 0) return;

    const bool taCntMode = (timerAControl & 0x20) != 0;  // bit5: 1=CNT, 0=PHI2
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

        if (traceMgr && traceMgr->isEnabled())
        {
            traceMgr->recordCiaTimer(
                getCIANumber(),
                'A',
                timerA,
                true,
                makeCIAStamp()
            );
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

void CIA6526::updateTimerB(uint32_t cyclesElapsed)
{
    const bool tbStarted = (timerBControl & 0x01);
    const bool tbCNT     = (timerBControl & 0x20);
    const bool tbCascade = (timerBControl & 0x40);

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

void CIA6526::handleTimerBCascade()
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

void CIA6526::latchTODClock()
{
    todLatch[0] = todClock[0];
    todLatch[1] = todClock[1];
    todLatch[2] = todClock[2];
    todLatch[3] = todClock[3];
    todLatched = true;
}

void CIA6526::incrementTODClock(uint32_t& todTicks, uint8_t todClock[], uint32_t todIncrementThreshold)
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

void CIA6526::checkTODAlarm(uint8_t todClock[], const uint8_t todAlarm[], bool& todAlarmTriggered, uint8_t& interruptStatus, uint8_t interruptEnable)
{
    if (todClock[0] == todAlarm[0] && todClock[1] == todAlarm[1] &&
        todClock[2] == todAlarm[2] && todClock[3] == todAlarm[3])
    {
        if (!todAlarmTriggered)
        {
            todAlarmTriggered = true;
            triggerInterrupt(INTERRUPT_TOD_ALARM);
        }
    }
}

void CIA6526::cntChangedA()
{
    const bool taStarted = (timerAControl & 0x01) != 0;
    const bool taCNT     = (timerAControl & 0x20) != 0;

    // Timer A counts CNT falling edges only when CRA bit5 selects CNT.
    if (!taStarted || !taCNT)
        return;

    uint32_t current = timerA ? timerA : 0x10000;

    if (--current == 0)
    {
        triggerInterrupt(INTERRUPT_TIMER_A);

        TraceManager* traceMgr = getTraceManager();
        if (traceMgr && traceMgr->isEnabled())
        {
            traceMgr->recordCiaTimer(
                getCIANumber(),
                'A',
                timerA,
                true,
                makeCIAStamp()
            );
        }

        // Serial shift on Timer A underflow if CRA bit6 is enabled.
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

        // Cascade Timer B if CRB bit6 selects Timer A underflows.
        if (timerBControl & 0x40)
        {
            handleTimerBCascade();
        }

        const bool continuous = (timerAControl & 0x08) == 0;

        if (continuous)
        {
            timerA = static_cast<uint16_t>((timerAHighByte << 8) | timerALowByte);
        }
        else
        {
            timerA = 0;
            timerAControl &= static_cast<uint8_t>(~0x01);
        }
    }
    else
    {
        timerA = static_cast<uint16_t>(current);
    }
}

void CIA6526::cntChangedB()
{
    const bool tbStarted = (timerBControl & 0x01) != 0;
    const bool tbCNT     = (timerBControl & 0x20) != 0;
    const bool tbCascade = (timerBControl & 0x40) != 0;

    // Timer B counts CNT falling edges only when CRB selects CNT,
    // and not when CRB selects Timer A cascade.
    if (!tbStarted || !tbCNT || tbCascade)
        return;

    uint32_t current = timerB ? timerB : 0x10000;

    if (--current == 0)
    {
        triggerInterrupt(INTERRUPT_TIMER_B);

        TraceManager* traceMgr = getTraceManager();
        if (traceMgr && traceMgr->isEnabled())
        {
            traceMgr->recordCiaTimer(
                getCIANumber(),
                'B',
                timerB,
                true,
                makeCIAStamp()
            );
        }

        const bool continuous = (timerBControl & 0x08) == 0;

        if (continuous)
        {
            timerB = static_cast<uint16_t>((timerBHighByte << 8) | timerBLowByte);
        }
        else
        {
            timerB = 0;
            timerBControl &= static_cast<uint8_t>(~0x01);
        }
    }
    else
    {
        timerB = static_cast<uint16_t>(current);
    }
}

void CIA6526::updateIRQLine()
{
    const bool active = (interruptStatus & interruptEnable & 0x1F) != 0;
    irqLineChanged(active);
}

void CIA6526::saveBaseState(StateWriter& wrtr) const
{
    wrtr.writeU8(portA);
    wrtr.writeU8(portB);
    wrtr.writeU8(ddrA);
    wrtr.writeU8(ddrB);

    wrtr.writeU8(timerALowByte);
    wrtr.writeU8(timerAHighByte);
    wrtr.writeU8(timerBLowByte);
    wrtr.writeU8(timerBHighByte);

    wrtr.writeU8(timerAControl);
    wrtr.writeU8(timerBControl);

    wrtr.writeU8(interruptStatus);
    wrtr.writeU8(interruptEnable);

    wrtr.writeU8(serialDataRegister);

    for (int i = 0; i < 4; ++i)
        wrtr.writeU8(todClock[i]);

    for (int i = 0; i < 4; ++i)
        wrtr.writeU8(todAlarm[i]);
}

bool CIA6526::loadBaseState(StateReader& rdr)
{
    if (!rdr.readU8(portA))                 return false;
    if (!rdr.readU8(portB))                 return false;
    if (!rdr.readU8(ddrA))                  return false;
    if (!rdr.readU8(ddrB))                  return false;

    if (!rdr.readU8(timerALowByte))         return false;
    if (!rdr.readU8(timerAHighByte))        return false;
    if (!rdr.readU8(timerBLowByte))         return false;
    if (!rdr.readU8(timerBHighByte))        return false;

    if (!rdr.readU8(timerAControl))         return false;
    if (!rdr.readU8(timerBControl))         return false;

    if (!rdr.readU8(interruptStatus))       return false;
    if (!rdr.readU8(interruptEnable))       return false;

    if (!rdr.readU8(serialDataRegister))    return false;

    for (int i = 0; i < 4; ++i)
        if (!rdr.readU8(todClock[i]))       return false;

    for (int i = 0; i < 4; ++i)
        if (!rdr.readU8(todAlarm[i]))       return false;

    return true;
}

void CIA6526::saveBaseRuntimeState(StateWriter& wrtr) const
{
    wrtr.writeU8(static_cast<uint8_t>(mode_));

    wrtr.writeU16(timerA);
    wrtr.writeU16(timerASnap);
    wrtr.writeBool(timerALatched);

    wrtr.writeU16(timerB);
    wrtr.writeU16(timerBSnap);
    wrtr.writeBool(timerBLatched);

    for (int i = 0; i < 4; ++i)
        wrtr.writeU8(todLatch[i]);

    wrtr.writeBool(todLatched);
    wrtr.writeU32(todTicks);

    wrtr.writeBool(todAlarmSetMode);
    wrtr.writeBool(todAlarmTriggered);

    wrtr.writeBool(cntLevel);
    wrtr.writeBool(lastCNT);

    wrtr.writeU8(shiftReg);
    wrtr.writeU8(shiftCount);
}

bool CIA6526::loadBaseRuntimeState(StateReader& rdr)
{
    uint8_t modeTemp = 0;
    if (!rdr.readU8(modeTemp))              return false;
    mode_ = static_cast<VideoMode>(modeTemp);

    if (!rdr.readU16(timerA))               return false;
    if (!rdr.readU16(timerASnap))           return false;
    if (!rdr.readBool(timerALatched))       return false;

    if (!rdr.readU16(timerB))               return false;
    if (!rdr.readU16(timerBSnap))           return false;
    if (!rdr.readBool(timerBLatched))       return false;

    for (int i = 0; i < 4; ++i)
        if (!rdr.readU8(todLatch[i]))       return false;

    if (!rdr.readBool(todLatched))          return false;
    if (!rdr.readU32(todTicks))             return false;

    if (!rdr.readBool(todAlarmSetMode))     return false;
    if (!rdr.readBool(todAlarmTriggered))   return false;

    if (!rdr.readBool(cntLevel))            return false;
    if (!rdr.readBool(lastCNT))             return false;

    if (!rdr.readU8(shiftReg))              return false;
    if (!rdr.readU8(shiftCount))            return false;

    return true;
}

void CIA6526::postLoadState()
{
    // Normalize
    timerAControl &= 0xEF;
    timerBControl &= 0xEF;

    setMode(mode_);

    refreshMasterBit();
    updateIRQLine();
}

void CIA6526::triggerInterrupt(InterruptBit interruptBit)
{
    interruptStatus |= interruptBit; // Set the relevant bit in the status register

    if (traceMgr && traceMgr->isEnabled())
    {
        traceMgr->recordCiaICR(
            getCIANumber(),
            interruptStatus,
            (interruptStatus & interruptEnable & 0x1F) != 0,
            makeCIAStamp()
        );
    }

    refreshMasterBit();
    updateIRQLine();
}

void CIA6526::clearIFR(InterruptBit interruptBit)
{
    interruptStatus &= ~interruptBit;

    if (traceMgr && traceMgr->isEnabled())
    {
        traceMgr->recordCiaICR(
            getCIANumber(),
            interruptStatus,
            (interruptStatus & interruptEnable & 0x1F) != 0,
            makeCIAStamp()
        );
    }

    refreshMasterBit();
    updateIRQLine();
}

void CIA6526::refreshMasterBit()
{
    if ((interruptStatus & 0x1F) != 0)
        interruptStatus |= 0x80;
    else
        interruptStatus &= static_cast<uint8_t>(~0x80);

    if (traceMgr && traceMgr->isEnabled())
    {
        traceMgr->recordCiaICR(
            getCIANumber(),
            interruptStatus,
            (interruptStatus & interruptEnable & 0x1F) != 0,
            makeCIAStamp()
        );
    }
}

std::string CIA6526::dumpRegisters(const std::string& group) const
{
    std::stringstream out;
    out << std::hex << std::uppercase << std::setfill('0');

    auto hex2 = [&](uint8_t v)
    {
        std::ostringstream s;
        s << std::hex << std::uppercase << std::setfill('0')
          << std::setw(2) << int(v);
        return s.str();
    };

    auto hex4 = [&](uint16_t v)
    {
        std::ostringstream s;
        s << std::hex << std::uppercase << std::setfill('0')
          << std::setw(4) << v;
        return s.str();
    };

    // Compute generic effective port values.
    // CIA input bits read high unless externally driven by the derived chip.
    uint8_t invA = static_cast<uint8_t>(~ddrA);
    uint8_t invB = static_cast<uint8_t>(~ddrB);
    uint8_t effA = static_cast<uint8_t>((portA & ddrA) | invA);
    uint8_t effB = static_cast<uint8_t>((portB & ddrB) | invB);

    // Ports
    if (group == "port" || group == "all")
    {
        out << "\nPort Registers\n\n";

        out << "PORT A (latch)             = $" << hex2(portA) << "\n";
        out << "PORT A DDR                 = $" << hex2(ddrA) << "\n";
        out << "PORT A (effective)         = $" << hex2(effA) << "\n";

        out << "PORT B (latch)             = $" << hex2(portB) << "\n";
        out << "PORT B DDR                 = $" << hex2(ddrB) << "\n";
        out << "PORT B (effective)         = $" << hex2(effB) << "\n";
    }

    // Timers
    if (group == "timer" || group == "all")
    {
        auto decodeCRA = [&](uint8_t cr)
        {
            std::ostringstream s;

            s << "Start:" << ((cr & 0x01) ? "On" : "Off")
              << " PBON:" << ((cr & 0x02) ? "Yes" : "No")
              << " OUTMODE:" << ((cr & 0x04) ? "Toggle" : "Pulse")
              << " Mode:" << ((cr & 0x08) ? "One-shot" : "Continuous")
              << " Load:" << ((cr & 0x10) ? "Yes" : "No")
              << " Clock:" << ((cr & 0x20) ? "CNT" : "PHI2");

            return s.str();
        };

        auto decodeCRB = [&](uint8_t cr)
        {
            std::ostringstream s;

            s << "Start:" << ((cr & 0x01) ? "On" : "Off")
              << " PBON:" << ((cr & 0x02) ? "Yes" : "No")
              << " OUTMODE:" << ((cr & 0x04) ? "Toggle" : "Pulse")
              << " Mode:" << ((cr & 0x08) ? "One-shot" : "Continuous")
              << " Load:" << ((cr & 0x10) ? "Yes" : "No")
              << " Clock:" << (((cr & 0x60) == 0x00) ? "PHI2" :
                                ((cr & 0x60) == 0x20) ? "CNT" :
                                ((cr & 0x60) == 0x40) ? "TimerA" :
                                                         "TimerA+CNT");

            return s.str();
        };

        out << "\nTimer Registers\n\n";

        out << "Timer A Latch Low          = $" << hex2(timerALowByte)  << "\n";
        out << "Timer A Latch High         = $" << hex2(timerAHighByte) << "\n";
        out << "Timer A Latched            = " << (timerALatched ? "Yes" : "No")
            << "  Snapshot = $" << hex4(timerASnap)
            << " (High will return snapshot: "
            << (timerALatched ? "Yes" : "No") << ")\n";
        out << "Timer A Current            = $" << hex4(timerA) << "\n";
        out << "Timer A Control Register   = $" << hex2(timerAControl)
            << "  [" << decodeCRA(timerAControl) << "]\n";

        out << "Timer B Latch Low          = $" << hex2(timerBLowByte)  << "\n";
        out << "Timer B Latch High         = $" << hex2(timerBHighByte) << "\n";
        out << "Timer B Latched            = " << (timerBLatched ? "Yes" : "No")
            << "  Snapshot = $" << hex4(timerBSnap)
            << " (High will return snapshot: "
            << (timerBLatched ? "Yes" : "No") << ")\n";
        out << "Timer B Current            = $" << hex4(timerB) << "\n";
        out << "Timer B Control Register   = $" << hex2(timerBControl)
            << "  [" << decodeCRB(timerBControl) << "]\n";
    }

    // TOD
    if (group == "tod" || group == "all")
    {
        out << "\nTOD Registers\n\n";

        out << "Current TOD                = "
            << hex2(todClock[3]) << ":"
            << hex2(todClock[2]) << ":"
            << hex2(todClock[1]) << "."
            << hex2(todClock[0]) << "\n";

        out << "TOD Alarm                  = "
            << hex2(todAlarm[3]) << ":"
            << hex2(todAlarm[2]) << ":"
            << hex2(todAlarm[1]) << "."
            << hex2(todAlarm[0]) << "\n";

        out << "TOD Latch                  = "
            << hex2(todLatch[3]) << ":"
            << hex2(todLatch[2]) << ":"
            << hex2(todLatch[1]) << "."
            << hex2(todLatch[0]) << "\n";

        out << "TOD Alarm Set Mode         = "
            << (todAlarmSetMode ? "Yes" : "No") << "\n";
    }

    // Interrupts
    if (group == "icr" || group == "all")
    {
        uint8_t sources = static_cast<uint8_t>(interruptStatus & 0x1F);
        bool masterPending = (sources & interruptEnable) != 0;

        out << "\nInterrupt Registers\n\n";

        out << "Interrupt Status (IFR)     = $" << hex2(interruptStatus) << "  [";

        if (interruptStatus & INTERRUPT_TIMER_A)
            out << " TA";

        if (interruptStatus & INTERRUPT_TIMER_B)
            out << " TB";

        if (interruptStatus & INTERRUPT_TOD_ALARM)
            out << " TOD";

        if (interruptStatus & INTERRUPT_SERIAL_SHIFT_REGISTER)
            out << " SR";

        if (interruptStatus & INTERRUPT_FLAG_LINE)
            out << " FLAG";

        out << " ]  Master Pending: "
            << (masterPending ? "Yes" : "No") << "\n";

        out << "Interrupt Enable (IER)     = $"
            << hex2(interruptEnable) << "\n";
    }

    // Serial
    if (group == "serial" || group == "all")
    {
        out << "\nSerial Register\n\n";
        out << "Serial Data Register       = $"
            << hex2(serialDataRegister) << "\n";
    }

    return out.str();
}

void CIA6526::setIERExact(uint8_t mask)
{
    mask &= 0x1F;
    interruptEnable = mask;
    updateIRQLine();
}
