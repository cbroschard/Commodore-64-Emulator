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
    if (cass && cass->isCassetteLoaded())
    {
        prevFlag = cass->getData(); // sample actual line level
    }
    else
    {
        prevFlag = true; // default pulled-up
    }

    // Mode
    inputMode = InputMode::modeProcessor;
}

uint8_t CIA1::readRegister(uint16_t address)
{
    switch(address)
    {
        case 0xDC00: // Port A
        {
            uint8_t busInputs = 0xFF;
            if (!(dataDirectionPortA & 0x10) && mem && mem->getCassetteSenseLow() && cass && cass->isCassetteLoaded() && cass->isPlayPressed())
            {
                busInputs &= ~0x10;
            }
            else
            {
                busInputs |= 0x10;
            }
            if (joy2)
            {
                busInputs = (busInputs & ~0x1F) | (joy2->getState() & 0x1F);
            }
            portAValue = (portA & dataDirectionPortA) | (~dataDirectionPortA & busInputs);
            return portAValue;
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
            // Acknowledge the interrupt by clearing it
            clearInterrupt(INTERRUPT_SERIAL_SHIFT_REGISTER);
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

            return result;
        }
        case 0xDC0E: // Timer A Control register
            return timerAControl;
        case 0xDC0F: // Timer B Control register
            return timerBControl;
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
            if (!(timerAControl & 0x01))
            {
                timerA = (timerAHighByte << 8) | timerALowByte;
            }
            break;
        }
        case 0xDC05: // Timer A high
        {
            timerAHighByte = value;
            if (!(timerAControl & 0x01))
            {
                timerA = (timerAHighByte << 8) | timerALowByte;
            }
            break;
        }
        case 0xDC06: // Timer B low byte
        {
            timerBLowByte = value;
            if (!(timerBControl & 0x01))
            {
                timerB = (timerBHighByte << 8) | timerBLowByte;
            }
            break;
        }
        case 0xDC07: // Timer B High
        {
            timerBHighByte = value;
            if (!(timerBControl & 0x01))
            {
                timerB = (timerBHighByte << 8) | timerBLowByte;
            }
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
            }
            else
            {
                interruptEnable &= ~mask;
            }
            refreshMasterBit();
            updateIRQLine();
            break;
        }
        case 0xDC0E: // Timer A control register
        {
            timerAControl = value & 0xEF;

            inputMode = (value & 0x20) ? InputMode::modeCNT : InputMode::modeProcessor;

            if (value & 0x10)
            {
                timerA = (timerAHighByte << 8) | timerALowByte;
                clearInterrupt(INTERRUPT_TIMER_A); // force-load clears IFR bit 0
            }

            if (!(value & 0x01))
            {
                clearInterrupt(INTERRUPT_TIMER_A); // no pending under-flow
            }
            break;
        }
        case 0xDC0F: // Timer B control register
        {
            todAlarmSetMode = (value & 0x80) != 0; // Bit 7 for the TOD Alarm toggle
            if (todAlarmSetMode)
            {
                todAlarmTriggered = false;
            }
            uint8_t crb = value & 0x7F; // mask out bit 7 before calculating timer B
            bool loadB = (crb & 0x10) !=0;
            timerBControl = crb & 0xEF;

            if (loadB) // Load bit set
            {
               timerB = (timerBHighByte << 8) | timerBLowByte; // Reload from latch
               clearInterrupt(INTERRUPT_TIMER_B);
            }
            if (crb & 0x01) // Start bit set
            {
                if (timerB == 0)
                {
                     timerB = (timerBHighByte << 8) | timerBLowByte; // Start from latch
                }
            }

            // Acknowledge any pending Timer B IRQ by clearing it when the timer is stopped
            if (!(timerBControl & 0x01))
            {
                clearInterrupt(INTERRUPT_TIMER_B);
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

uint32_t CIA1::calculatePrescaler(uint8_t control)
{
    static constexpr uint32_t table[4] = { 1, 1, 4, 16 };
    uint8_t idx = (control >> 5) & 0x03;  // use bits 5–6, not 1–2
    return table[idx];
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
    // Handle Timer A
    updateTimerA(cyclesElapsed);

    // Handle Timer B
    updateTimerB(cyclesElapsed);

    // Update TOD Clock
    todTicks += cyclesElapsed;
    while (todTicks >= todIncrementThreshold)
    {
        incrementTODClock(todTicks, todClock, todIncrementThreshold);
    }

    // Check TOD Alarm
    checkTODAlarm(todClock, todAlarm, todAlarmTriggered, interruptStatus, interruptEnable);

    // Cassette handler
    for (uint32_t i = 0; i < cyclesElapsed; ++i)
    {
        if (cass && cass->motorOn() && cass->isCassetteLoaded())
        {
            cass->tick();
        }

        bool currFlag = (cass && cass->isCassetteLoaded()) ? cass->getData() : true;
        // Detect falling edge (1 to 0) and raise IRQ
        if (prevFlag && !currFlag)
        {
            triggerInterrupt(INTERRUPT_FLAG_LINE); // raise IRQ if enabled
        }
        prevFlag = currFlag;
    }
}

void CIA1::cntChanged()
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
            if (timerBControl & 0x20) // cascade B if requested
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

void CIA1::updateTimerA(uint32_t cyclesElapsed)
{
    if (inputMode == InputMode::modeTimerA)
    {
        return;
    }

    if (!(timerAControl & 0x01) || cyclesElapsed == 0)
    {
        return;
    }
    // accumulate CPU cycles
    timerACycleCount += cyclesElapsed;
    uint32_t prescale = calculatePrescaler(timerAControl);

    // tick once per prescaler interval
    while (timerACycleCount >= prescale)
    {
        timerACycleCount -= prescale;
        // treat 0 as 0x10000
        uint32_t current = timerA ? timerA : 0x10000;
        if (current > 1)
        {
            timerA = static_cast<uint16_t>(current - 1);
            continue;
        }

        // underflow
        triggerInterrupt(INTERRUPT_TIMER_A);
        bool continuous = !(timerAControl & 0x08);

        if (continuous)
        {
            timerA = (timerAHighByte << 8) | timerALowByte;
        }
        else
        {
            timerA = 0;
            timerAControl &= ~0x01;  // stop
            break;
        }
        if (timerBControl & 0x20)
        {
            handleTimerBCascade();
        }

        // Handle serial shift on underflow
        if (timerAControl & 0x40)
        {
            bool cnt = cass && cass->isCassetteLoaded() ? cass->getData() : true;
            uint8_t bit = cnt ? 1 : 0;
            shiftReg = (shiftReg << 1) | bit;
            if (++shiftCount == 8) // If full byte process it
            {
                serialDataRegister = shiftReg;
                shiftCount = 0; // reset the counter
                triggerInterrupt(INTERRUPT_SERIAL_SHIFT_REGISTER);
            }
        }
   }
}

void CIA1::updateTimerB(uint32_t cyclesElapsed)
{
    // if cascade bit set, we’ll drive B from A underflows later
    if ((timerBControl & 0x20) || !(timerBControl & 0x01) || cyclesElapsed == 0)
    {
        return;
    }

    timerBCycleCount += cyclesElapsed;
    uint32_t prescale = calculatePrescaler(timerBControl);

    while (timerBCycleCount >= prescale)
    {
        timerBCycleCount -= prescale;
        uint32_t current = timerB ? timerB : 0x10000;
        if (current > 1)
        {
            timerB = static_cast<uint16_t>(current - 1);
            continue;
        }

        // underflow
        triggerInterrupt(INTERRUPT_TIMER_B);
        bool continuous = !(timerBControl & 0x08);

        if (continuous)
        {
            timerB = (timerBHighByte << 8) | timerBLowByte;
        }
        else
        {
            timerB = 0;
            timerBControl &= ~0x01;  // stop
            break;
        }
    }
}

void CIA1::handleTimerBCascade()
{
    // only on A-underflow when B’s cascade bit is set
    // (0x20 = bit-5)
    if (!(timerBControl & 0x20) || !(timerBControl & 0x01))
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
            if (interruptEnable & INTERRUPT_TOD_ALARM) // TOD interrupt enabled
            {
                triggerInterrupt(INTERRUPT_TOD_ALARM);
            }
        }
    }
    else
    {
        todAlarmTriggered = false; // Reset when TOD clock no longer matches alarm
    }
}

void CIA1::triggerInterrupt(InterruptBit interruptBit)
{
    interruptStatus |= interruptBit; // Set the relevant bit in the status register

    // Check if the interrupt is enabled
    if (interruptEnable & interruptBit)
    {
        interruptStatus |= 0x80; // Set the master interrupt bit

        // Map the interrupt to the correct IRQ source
        IRQLine::Source source = IRQLine::NONE;
        switch (interruptBit)
        {
            case INTERRUPT_TIMER_A:
                source = IRQLine::CIA1_TIMER_A;
                break;
            case INTERRUPT_TIMER_B:
                source = IRQLine::CIA1_TIMER_B;
                break;
            case INTERRUPT_TOD_ALARM:
                source = IRQLine::CIA1_TOD;
                break;
            case INTERRUPT_SERIAL_SHIFT_REGISTER:
                source = IRQLine::CIA1_SERIAL;
                break;
            case INTERRUPT_FLAG_LINE:
                source = IRQLine::CIA1_FLAG;
                break;
        }

        // Raise the interrupt on the IRQ line
        if (source != IRQLine::NONE && IRQ)
        {
            IRQ->raiseIRQ(source);
        }
    }
    else
    {
        if (logger)
        {
            logger->WriteLog("Interrupt requested but interrupts are disabled in CIA1");
        }
    }
}

void CIA1::updateIRQLine()
{
    if (!IRQ)
    {
        return;
    }

    // Process all bits
    const bool tA = interruptStatus & interruptEnable & INTERRUPT_TIMER_A;
    const bool tB = interruptStatus & interruptEnable & INTERRUPT_TIMER_B;
    const bool tod = interruptStatus & interruptEnable & INTERRUPT_TOD_ALARM;
    const bool srl = interruptStatus & interruptEnable & INTERRUPT_SERIAL_SHIFT_REGISTER;
    const bool flg = interruptStatus & interruptEnable & INTERRUPT_FLAG_LINE;

    // raise or clear each source on the shared IRQ line
    tA  ? IRQ->raiseIRQ( IRQLine::CIA1_TIMER_A) : IRQ->clearIRQ( IRQLine::CIA1_TIMER_A);
    tB  ? IRQ->raiseIRQ( IRQLine::CIA1_TIMER_B) : IRQ->clearIRQ( IRQLine::CIA1_TIMER_B);
    tod ? IRQ->raiseIRQ( IRQLine::CIA1_TOD) : IRQ->clearIRQ( IRQLine::CIA1_TOD);
    srl ? IRQ->raiseIRQ( IRQLine::CIA1_SERIAL) : IRQ->clearIRQ( IRQLine::CIA1_SERIAL);
    flg ? IRQ->raiseIRQ( IRQLine::CIA1_FLAG) : IRQ->clearIRQ( IRQLine::CIA1_FLAG);
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
    if (interruptStatus & interruptEnable & 0x1F)
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

    // Port registers
    if (group == "port" || group == "all")
    {
        out << "\nPort Registers \n\n";
        out << "PORT A = $" << std::setw(2) << static_cast<int>(portA) << "\n";
        out << "PORT A Data Direction Register = $" << std::setw(2) << static_cast<int>(dataDirectionPortA) << "\n";
        out << "PORT B = $" << std::setw(2) << static_cast<int>(portB) << "\n";
        out << "PORT B Data Direction Register = $" << std::setw(2) << static_cast<int>(dataDirectionPortB) << "\n";
    }

    // Timer registers
    if (group == "timer" || group == "all")
    {
        out << "\nTimer Registers \n\n";
        out << "Timer A Latch Low = $" << std::setw(2) << static_cast<int>(timerALowByte) << "\n";
        out << "Timer A Latch High = $" << std::setw(2) << static_cast<int>(timerAHighByte) << "\n";
        out << "Timer A Latched = " << (timerALatched ? "Yes" : "No") << "  Snapshot = $" << std::setw(4) << timerASnap << "\n";
        out << "Timer A Current = $" << std::setw(4) << timerA << "\n";
        out << "Timer A Control Register = $" << std::setw(2) << static_cast<int>(timerAControl) <<  " Active: " << ((timerAControl & 0x01) ? "Yes" : "No")
            << " Mode: " << ((timerAControl & 0x08) ? "One shot" : "Continuous") << "\n";
        out << "Timer B Latch Low = $" << std::setw(2) << static_cast<int>(timerBLowByte) << "\n";
        out << "Timer B Latch High = $" << std::setw(2) << static_cast<int>(timerBHighByte) << "\n";
        out << "Timer B Latched = " << (timerBLatched ? "Yes" : "No") << "  Snapshot = $" << std::setw(4) << timerBSnap << "\n";
        out << "Timer B Current = $" << std::setw(4) << timerB << "\n";
        out << "Timer B Control Register = $" << std::setw(2) << static_cast<int>(timerBControl) << " Active: " << ((timerBControl & 0x01) ? "Yes" : "No")
            << " Mode: " << ((timerBControl & 0x08) ? "One shot" : "Continuous") << "\n";
    }

    // TOD registers
    if (group == "tod" || group == "all")
    {
        out << "\nTOD Registers\n\n";

        out << "Current TOD = "
            << std::setw(2) << static_cast<int>(todClock[3]) << ":"
            << std::setw(2) << static_cast<int>(todClock[2]) << ":"
            << std::setw(2) << static_cast<int>(todClock[1]) << "."
            << std::setw(2) << static_cast<int>(todClock[0]) << "\n";

        out << "TOD Alarm   = "
            << std::setw(2) << static_cast<int>(todAlarm[3]) << ":"
            << std::setw(2) << static_cast<int>(todAlarm[2]) << ":"
            << std::setw(2) << static_cast<int>(todAlarm[1]) << "."
            << std::setw(2) << static_cast<int>(todAlarm[0]) << "\n";

        out << "TOD Latch   = "
            << std::setw(2) << static_cast<int>(todLatch[3]) << ":"
            << std::setw(2) << static_cast<int>(todLatch[2]) << ":"
            << std::setw(2) << static_cast<int>(todLatch[1]) << "."
            << std::setw(2) << static_cast<int>(todLatch[0]) << "\n";

        out << "TOD Alarm Set Mode = " << (todAlarmSetMode ? "Yes" : "No") << "\n";
    }

    // Interrupt status
    if (group == "icr" || group == "all")
    {
        out << "\nInterrupt Registers\n\n";
        out << "Interrupt Status (IFR) = $" << std::setw(2) << static_cast<int>(interruptStatus)
            << "  [";
        if (interruptStatus & INTERRUPT_TIMER_A) out << " TA";
        if (interruptStatus & INTERRUPT_TIMER_B) out << " TB";
        if (interruptStatus & INTERRUPT_TOD_ALARM) out << " TOD";
        if (interruptStatus & INTERRUPT_SERIAL_SHIFT_REGISTER) out << " SR";
        if (interruptStatus & INTERRUPT_FLAG_LINE) out << " FLAG";
        out << " ]\n";

        out << "Interrupt Enable (IER) = $" << std::setw(2) << static_cast<int>(interruptEnable) << "\n";
    }

    // Serial data register
    if (group == "serial" || group == "all")
    {
        out << "\nSerial Register\n\n";
        out << "Serial Data Register = $" << std::setw(2) << static_cast<int>(serialDataRegister) << "\n";
    }

    // Mode
    if (group == "mode" || group == "all")
    {
        out << "\nControl Lines\n\n";
        out << "CNT Input Mode = ";
        switch (inputMode)
        {
            case InputMode::modeProcessor: out << "Processor polling"; break;
            case InputMode::modeCNT:       out << "CNT-driven"; break;
            case InputMode::modeTimerA:    out << "Timer A-driven"; break;
            case InputMode::modeTimerACNT: out << "Timer A + CNT"; break;
        }
        out << "\n";
        out << "Previous FLAG state = " << (prevFlag ? "High" : "Low") << "\n";
    }

    return out.str();
}
