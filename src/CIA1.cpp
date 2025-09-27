// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "cia1.h"

CIA1::CIA1()
{
    setMode(VideoMode::NTSC);
}

CIA1::~CIA1()
{

}

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

void CIA1::setMode(VideoMode mode)
{
    mode_ = mode;
    todIncrementThreshold = (mode_ == VideoMode::NTSC) ? 102273 : 98525;
}

void CIA1::reset() {
    // Ports & DDRs
    portAValue = 0;
    portA = 0xFF;
    portB = 0xFF;
    dataDirectionPortA = 0x00;
    dataDirectionPortB = 0x00;
    rowState = 0xFF;  // inputs float high
    activeRow = 0;
    rowIndex = 0;

    // Timers
    timerA = timerB = 0;
    timerALowByte = timerAHighByte = 0;
    timerBLowByte = timerBHighByte = 0;
    timerACycleCount = timerBCycleCount = 0;
    timerAControl = timerBControl = 0;

    // Timer Lathes
    timerASnap = timerBSnap = 0;
    timerALatched = timerBLatched = false;

    // Serial shift register state
    shiftCount = shiftReg = 0;

    // TOD
    std::fill(std::begin(todClock), std::end(todClock),0);
    std::fill(std::begin(todAlarm), std::end(todAlarm),0xFF);
    std::fill(std::begin(todLatch), std::end(todLatch),0);
    todLatched = false;
    todAlarmSetMode = false;
    todAlarmTriggered = false;

    // Serial / IRQ
    serialDataRegister = 0;
    interruptStatus = 0;
    interruptEnable = 0;

    // Update the cassette
    prevReadLevel = true;
    cassetteReadLineLevel = true;
    gateWasOpenPrev = false;

    if (cass && cass->isCassetteLoaded())
    {
        prevReadLevel = cass->getData(); // sample actual line level
    }
    else
    {
        prevReadLevel = true; // default pulled-up
    }

    // Mode
    inputMode = InputMode::modeProcessor;

    // CNT
    cntLevel = true;
    lastCNT = true;

    // Clear all IRQ's
    if (IRQ) IRQ->clearIRQ(IRQLine::CIA1);

    refreshMasterBit();
    updateIRQLine();
}

uint8_t CIA1::readRegister(uint16_t address)
{
    switch(address)
    {
        case 0xDC00: // CIA1 Port A read
        {
            const uint8_t ddra = dataDirectionPortA;
            const uint8_t ddrb = dataDirectionPortB;
            const uint8_t prb  = portB;

            uint8_t pin = 0xFF;  // pull-ups
            if (keyb)  // merge keyboard-selected rows
                for (int r = 0; r < 8; ++r)
                    if ((ddrb & (1u<<r)) && !(prb & (1u<<r)))
                        pin &= keyb->readRow(r);          // 0 = pressed

            if (joy2)
            {  // merge joystick 2 (active-low like joy1)
                uint8_t st = joy2->getState() | 0xE0;     // PA5..PA7 stay high
                pin &= st;                                // 0 = pressed
            }

            const uint8_t v = (portA & ddra) | (pin & ~ddra);
            portAValue = v;
            return v;
        }
        case 0xDC01: // Port B
        {
            uint8_t pa = (portA & dataDirectionPortA) | (~dataDirectionPortA & 0xFF);
            // Calculate the active row
            if (pa == 0xFF)
            {
                rowState = 0xFF;
            }
            else
            {
                // Find the active rows so the keyboard keys work
                uint8_t combinedRowState = 0xFF;
                for (uint8_t i = 0; i < 8; i++)
                {
                    //if ((portA & (1 << i)) == 0)
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
                rowState &= joy1->getState();
            }

            // Combine PortB and row state
            return (portB & dataDirectionPortB) | (rowState & ~dataDirectionPortB);
        }
        case 0xDC02: // Data direction register for Port A
            return dataDirectionPortA;
        case 0xDC03: // Data direction port B
            return dataDirectionPortB;
        case 0xDC04: // Timer A low byte
            timerASnap = timerA;
            timerALatched = true;
            return timerASnap & 0xFF;
        case 0xDC05: // Timer A high byte
        {
            if (timerALatched)
            {
                timerALatched = false;
                return timerASnap >> 8;
            }
            return timerA >> 8;
        }
        case 0xDC06: // Timer B low byte
            timerBSnap = timerB;
            timerBLatched = true;
            return timerBSnap & 0xFF;
        case 0xDC07: // Timer B high byte
        {
            if (timerBLatched)
            {
                timerBLatched = false;
                return timerBSnap >> 8;
            }
            return timerB >> 8;
        }
        case 0xDC08: // TOD Clock 1/10 seconds
            if (!todLatched)
            {
                latchTODClock();
            }
            return binaryToBCD(todLatch[0]);
        case 0xDC09: // TOD Clock seconds
            if (!todLatched)
            {
                latchTODClock();
            }
            return binaryToBCD(todLatch[1]);
        case 0xDC0A: // TOD Clock minutes
            if (!todLatched)
            {
                latchTODClock();
            }
            return binaryToBCD(todLatch[2]);
        case 0xDC0B: // TOD Clock hours and alarm control
            if (!todLatched)
            {
                latchTODClock();
            }
            todLatched = false;
            return binaryToBCD(todLatch[3]);
        case 0xDC0C:
        {
            return serialDataRegister;
        }
        case 0xDC0D: // Interrupt control register
        {
            uint8_t result = interruptStatus & 0x1F; // latch bits 0-4 only

            // If any enabled source is active, set bit 7 in the *returned value*
            if (result & interruptEnable)
            {
                result |= 0x80;
            }

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
        case 0xDC0E: // Timer A Control register
            return timerAControl & 0x7F;
        case 0xDC0F: // Timer B Control register
            return timerBControl & 0x7F;
        default:
            if (logger)
            {
                logger->WriteLog("Unhandled address requested in CIA1 read register. Address requested = " + std::to_string(address));
            }
            return 0xFF;
    }
    return 0xFF; // default return value if not handled
}

void CIA1::writeRegister(uint16_t address, uint8_t value)
{
    switch(address)
    {
        case 0xDC00: // Port A
        {
            portA = value;
            break;
        }
        case 0xDC01: // Port B
        {
            portB = value;
            break;
        }
        case 0xDC02: // Data direction port A
        {
            dataDirectionPortA = value;
            break;
        }
        case 0xDC03: // Data direction port B
        {
            dataDirectionPortB = value;
            break;
        }
        case 0xDC04: // Timer A low byte
        {
            timerALowByte = value;
            break;
        }
        case 0xDC05: // Timer A high
        {
            timerAHighByte = value;
            break;
        }
        case 0xDC06: // Timer B low byte
        {
            timerBLowByte = value;
            break;
        }
        case 0xDC07: // Timer B High
        {
            timerBHighByte = value;
            break;
        }
        case 0xDC08: // TOD clock 1/10 seconds
            if (todAlarmSetMode) // Writing to alarm
            {
                todAlarm[0] = bcdToBinary(value & 0x0F); // Update TOD alarm 1/10 seconds
                todAlarmTriggered = false;
            }
            else // Writing to clock
            {
                todClock[0] = bcdToBinary(value &0x0F); // Update TOD clock 1/10 seconds
                 if (todLatched)
                 {
                     todLatch[0] = todClock[0];
                 }
            }
            break;
        case 0xDC09: // TOD clock seconds
            if (todAlarmSetMode) // Writing to alarm
            {
                todAlarm[1] = bcdToBinary(value & 0x7F); // Update TOD alarm seconds
                todAlarmTriggered = false;
            }
            else // Writing to clock
            {
                todClock[1] = bcdToBinary(value & 0x7F);
                if (todLatched)
                {
                    todLatch[1] = todClock[1];
                }
            }
            break;
        case 0xDC0A: // TOD clock minutes
            if (todAlarmSetMode) // Writing to alarm
            {
                todAlarm[2] = bcdToBinary(value & 0x7F); // Update TOD alarm seconds
                todAlarmTriggered = false;
            }
            else // Writing to clock
            {
                todClock[2] = bcdToBinary(value & 0x7F);
                if (todLatched)
                {
                    todLatch[2] = todClock[2];
                }
            }
            break;
        case 0xDC0B: // TOD clock hours and alarm control
            if (todAlarmSetMode) // Writing to alarm
            {
                todAlarm[3] = bcdToBinary(value & 0x3F); // Update TOD alarm hours
                todAlarmTriggered = false;
            }
            else // Writing to clock
            {
                todClock[3] = bcdToBinary(value & 0x3F);
                if (todLatched)
                {
                    todLatch[3] = todClock[3];
                }
            }
            break;
        case 0xDC0C: // Serial Data Register
            {
                if (logger)
                {
                    logger->WriteLog("Serial Data Register Write: value = " + std::to_string(value));
                }
                serialDataRegister = value;
            }

            break;
        case 0xDC0D: // Interrupt control register
        {
            uint8_t mask = value & 0x1F;

            if (value & 0x80)
            {
                interruptEnable |= mask;
                if (logger)
                {
                    logger->WriteLog("[CIA1] IER |= $" + toHex(mask,2) +
                             "  => IER now=$" + toHex(interruptEnable,2));
                }
            }
            else
            {
                interruptEnable &= ~mask;
                if (logger)
                {
                    logger->WriteLog("[CIA1] IER &= ~$" + toHex(mask,2) +
                             "  => IER now=$" + toHex(interruptEnable,2));
                }
            }

            refreshMasterBit();
            updateIRQLine();
            break;
        }
        case 0xDC0E:
        {
            const uint8_t old = timerAControl;
            const uint8_t cra = value & 0x7F;       // ignore bit7

            inputMode = (cra & 0x20) ? InputMode::modeCNT : InputMode::modeProcessor;

            // Bit4 = LOAD strobe
            if (cra & 0x10)
            {
                timerA = (timerAHighByte << 8) | timerALowByte;
                clearInterrupt(INTERRUPT_TIMER_A);
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
            break;
        }
        case 0xDC0F:
        {
            const uint8_t old = timerBControl;
            const uint8_t crb = value & 0x7F;       // ignore bit7

            // Bit4 = LOAD strobe
            if (crb & 0x10) {
                timerB = (timerBHighByte << 8) | timerBLowByte;
                clearInterrupt(INTERRUPT_TIMER_B);
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
            break;
        }
        default:
        {
            if (logger)
            {
                logger->WriteLog("Unhandled address requested in CIA1 write register. Address requested = " + std::to_string(address));
            }
            break;
        }
    }
}

void CIA1::latchTODClock()
{
    todLatch[0] = todClock[0];
    todLatch[1] = todClock[1];
    todLatch[2] = todClock[2];
    todLatch[3] = todClock[3];
    todLatched = true;
}

void CIA1::updateTimers(uint32_t cyclesElapsed)
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

            if (level != prevReadLevel)
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
}

void CIA1::cntChangedA()
{
    if (inputMode == InputMode::modeCNT && (timerAControl & 0x01))
    {
        // one CNT pulse == one timer tick
        uint32_t current = timerA ? timerA : 0x10000;
        if (--current == 0)
        {
            triggerInterrupt(INTERRUPT_TIMER_A);

            bool continuous = !(timerAControl & 0x08);
            timerA = continuous ? (timerAHighByte << 8) | timerALowByte : 0;


            if (!continuous) // stop on one-shot
            {
                timerAControl &= ~0x01;
            }
            if (timerBControl & 0x40) // cascade B if requested
            {
                handleTimerBCascade();
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
        }
    }
}

void CIA1::setCNTLine(bool level)
{
    const bool falling = (lastCNT && !level);
    lastCNT = cntLevel = level;
    if (!falling) return;

    // TA counts on CNT when CRA bit5=1 and START=1
    if ((timerAControl & 0x20) && (timerAControl & 0x01))
    {
        cntChangedA();
    }

    // TB counts on CNT when CRB bit5=1, START=1, and NOT cascade
    if ((timerBControl & 0x20) && (timerBControl & 0x01) && !(timerBControl & 0x40))
    {
        cntChangedB();
    }
}

void CIA1::triggerInterrupt(InterruptBit interruptBit)
{
    interruptStatus |= interruptBit; // Set the relevant bit in the status register

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

    refreshMasterBit();
    updateIRQLine();
}

void CIA1::refreshMasterBit()
{
    if ((interruptStatus & 0x1F) != 0)
    {
        interruptStatus |= 0x80;
    }
    else
    {
        interruptStatus &= 0x7F;
    }
}

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
