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

void CIA6526::updateIRQLine()
{
    const bool active = (interruptStatus & interruptEnable & 0x1F) != 0;
    irqLineChanged(active);
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
