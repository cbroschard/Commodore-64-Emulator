// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "cia2.h"

CIA2::CIA2()
{
    setMode(VideoMode::NTSC);
}

CIA2::~CIA2() = default;

void CIA2::reset() {
    // Ports & DDRs
    portA = 0xFF;
    portB = 0xFF;
    dataDirectionPortA = 0x00;
    dataDirectionPortB = 0x00;

    // Timers & tick state
    timerALowByte = 0x00;
    timerAHighByte = 0x00;
    timerBLowByte = 0x00;
    timerBHighByte = 0x00;
    timerA = 0;
    timerB = 0;
    ticksA = 0;
    ticksB = 0;
    clkSelA = 0;
    clkSelB = 0;

    // TOD
    todTicks = 0;
    todLatched = false;
    todAlarmSetMode = false;
    todAlarmTriggered = false;
    std::fill(std::begin(todClock), std::end(todClock), 0);
    std::fill(std::begin(todAlarm), std::end(todAlarm), 0);
    std::fill(std::begin(todLatch), std::end(todLatch), 0);

    // Timer control
    timerAControl = 0;
    timerBControl = 0;

    // Underflow and Pulse
    timerAUnderFlowFlag = false;
    timerAPulseFlag = false;

    // Interrupt / NMI
    interruptEnable = 0;
    interruptStatus = 0;
    status = 0;
    nmiAsserted = false;

    // Serial
    serialDataRegister = 0xFF;
    outBit = 7;

    // Cycle accumulators
    accumulatedCyclesA = 0;
    accumulatedCyclesB = 0;
}

void CIA2::setMode(VideoMode mode)
{
    mode_ = mode;
    todIncrementThreshold = (mode_ == VideoMode::NTSC) ? 102273 : 98525;
}

uint8_t CIA2::readRegister(uint16_t address)
{
    switch(address)
    {
        case 0xDD00:
        {
            uint8_t result = portA & dataDirectionPortA;

             if (bus && !(dataDirectionPortA & ATN_MASK))
            {
                if (bus->readAtnLine())
                {
                    result |= ATN_MASK;
                }
                else
                {
                    result &= ~ATN_MASK;
                }
            }
            if (bus && !(dataDirectionPortA & CLK_MASK))
            {
                if (bus->readClkLine())
                {
                    result |= CLK_MASK;
                }
                else
                {
                    result &= ~CLK_MASK;
                }
            }
            if (bus && !(dataDirectionPortA & DATA_MASK))
            {
                if (bus->readDataLine())
                {
                    result |= DATA_MASK;
                }
                else
                {
                    result &= ~DATA_MASK;
                }
            }
            uint8_t others = ~dataDirectionPortA & ~(ATN_MASK|CLK_MASK|DATA_MASK);
            result |= others;
            return result;
        }
        case 0xDD01: // Port B
        {
            uint8_t result = (portB & dataDirectionPortB) | (~dataDirectionPortB & 0xFF);
            // For each input bit override the tri-state
            if (!(dataDirectionPortB & RXD_MASK))
            {
                bool rxd = rs232dev ? rs232dev->getRXD() : true;
                result = rxd ? (result | RXD_MASK) : (result & ~RXD_MASK);
            }
            if (!(dataDirectionPortB & DSR_MASK))
            {
                bool dsr = rs232dev ? rs232dev->getDSR() : true;
                result = dsr ? (result | DSR_MASK) : (result & ~DSR_MASK);
            }
            if (!(dataDirectionPortB & CTS_MASK))
            {
                bool cts = rs232dev ? rs232dev->getCTS() : true;
                result = cts ? (result | CTS_MASK) : (result & ~CTS_MASK);
            }
            if (!(dataDirectionPortB & DCD_MASK))
            {
                bool dcd = rs232dev ? rs232dev->getDCD() : true;
                result = dcd ? (result | DCD_MASK) : (result & ~DCD_MASK);
            }
            if (!(dataDirectionPortB & RI_MASK))
            {
                bool ri = rs232dev ? rs232dev->getRI() : true;
                result = ri ? (result | RI_MASK) : (result & ~RI_MASK);
            }
            return result;
        }
        case 0xDD02: // Data direction Port A
            return dataDirectionPortA;
        case 0xDD03: // Data direction Port B
            return dataDirectionPortB;
        case 0xDD04: // Timer A low byte
            //return timerALowByte;
            return timerA & 0xFF;
        case 0xDD05: // Timer A high byte
            //return timerAHighByte;
            return (timerA >> 8) & 0xFF;
        case 0xDD06: // Timer B low byte
            return timerB & 0xFF;
        case 0xDD07: // Timer B high byte
            return (timerB >> 8) & 0xFF;
        case 0xDD08: // TOD Clock 1/10 seconds
            if (!todLatched)
            {
                latchTODClock();
            }
            return binaryToBCD(todLatch[0]);
        case 0xDD09: // TOD Clock seconds
            if (!todLatched)
            {
                latchTODClock();
            }
            return binaryToBCD(todLatch[1]);
        case 0xDD0A: // TOD Clock minutes
            if (!todLatched) {
                latchTODClock();
            }
            return binaryToBCD(todLatch[2]);
        case 0xDD0B: // TOD Clock hours
            if (!todLatched) {
                latchTODClock();
            }
            todLatched = false; // Clear latch after reading hours
            return binaryToBCD(todLatch[3]);
        case 0xDD0C: // Serial data register
            return serialDataRegister;
        case 0xDD0D: // Interrupt control register
            {
                uint8_t result = interruptStatus;
                if (interruptStatus & interruptEnable & 0x1F)
                {
                    result |= 0x80;
                }
                else
                {
                    result &= 0x7F;
                }
                refreshNMI();
                return result;
            }
        case 0xDD0E: // Timer A control
            return timerAControl & ~0x10;
        case 0xDD0F: // Timer B control
            return timerBControl & ~0x10 & ~0x80;
        default:
            return 0xFF; // default return if not handled
    }

    return 0x00; // default return if not handled
}

void CIA2::writeRegister(uint16_t address, uint8_t value)
{
    switch(address)
    {
        case 0xDD00:
        {  // Data Port A
            // Merge new outputs only on DDR=1 bits; keep all DDR=0 bits (VIC bank) intact
            portA = (portA & ~dataDirectionPortA) | (value &  dataDirectionPortA);

            // ATN (PA3)
            if (dataDirectionPortA & ATN_MASK && bus)
            {
                bus->setAtnLine( (portA & ATN_MASK) != 0 );
            }
            // CLK (PA4)
            if (dataDirectionPortA & CLK_MASK && bus)
            {
                bus->setClkLine( (portA & CLK_MASK) != 0 );
            }
            // DATA (PA5)
            if (dataDirectionPortA & DATA_MASK && bus)
            {
                bus->setDataLine((portA & DATA_MASK) != 0 );
            }
            break;
        }
        case 0xDD01: // Data port B
            {
                portB = (value & dataDirectionPortB) | (portB & ~dataDirectionPortB);
                // For each output bit, capture and later drive the line
                if (dataDirectionPortB & DTR_MASK)
                {
                    if (rs232dev)
                    {
                        rs232dev->setDTR((portB & DTR_MASK) != 0);
                    }
                }
                if (dataDirectionPortB & RTS_MASK)
                {
                    if (rs232dev)
                    {
                        rs232dev->setRTS((portB & RTS_MASK) != 0);
                    }
                }
                break;
            }
        case 0xDD02: // Data direction Port A
            dataDirectionPortA = value;
            break;
        case 0xDD03: // Data direction port B
            dataDirectionPortB = value;
            break;
        case 0xDD04: // Timer A low byte
            timerALowByte = value;
            timerA = (timerAHighByte << 8) | timerALowByte;
            break;
        case 0xDD05: // Timer A high byte
            timerAHighByte = value;
            timerA = (timerAHighByte << 8) | timerALowByte;
            break;
        case 0xDD06: // Timer B low byte
            timerBLowByte = value;
            timerB = (timerBHighByte << 8) | timerBLowByte;
            break;
        case 0xDD07: // Timer B high byte
            timerBHighByte = value;
            timerB = (timerBHighByte << 8) | timerBLowByte;
            break;
        case 0xDD08: // TOD clock 1/10 seconds
            if (todAlarmSetMode)
            {
                todAlarm[0] = bcdToBinary(value & 0x0F); // Update TOD alarm
                todAlarmTriggered = false;
            }
            else
            {
                todClock[0] = bcdToBinary(value & 0x0F); // Update TOD clock
            }
            break;
        case 0xDD09: // TOD clock seconds
            if (todAlarmSetMode)
            {
                todAlarm[1] = bcdToBinary(value & 0x7F); // Update TOD alarm
                todAlarmTriggered = false;
            }
            else
            {
                todClock[1] = bcdToBinary(value & 0x7F); // Update TOD clock
            }
            break;
        case 0xDD0A: // TOD clock minutes
            if (todAlarmSetMode)
            {
                todAlarm[2] = bcdToBinary(value & 0x7F); // Update TOD alarm
                todAlarmTriggered = false;
            }
            else
            {
                todClock[2] = bcdToBinary(value & 0x7F); // Update TOD clock
            }
            break;
        case 0xDD0B: // TOD clock hours and alarm clock
            if (todAlarmSetMode)
            {
                todAlarm[3] = bcdToBinary(value & 0x3F); // Update TOD alarm
                todAlarmTriggered = false;
            }
            else
            {
                todClock[3] = bcdToBinary(value & 0x3F); // Update TOD clock
            }
            break;
        case 0xDD0C: // Serial data register
            serialDataRegister = value;
            break;
        case 0xDD0D: // Interrupt control register
        {
            if (value & 0x80)
            {
                interruptEnable |= (value & 0x1F);
            }
            else
            {
                interruptStatus &= ~(value & 0x1F);
            }
            refreshNMI();
            break;
        }
        case 0xDD0E: // Timer A Control
            {
                uint8_t oldValue = timerAControl;
                timerAControl = value & 0x7F; // mask out last bit

                // Force load strobe
                if (value & 0x10)
                {
                    timerA = (timerAHighByte << 8) | timerALowByte;
                }
                if ((value & 0x01) && !(oldValue & 0x01))
                {
                    timerA = (timerAHighByte << 8) | timerALowByte;
                    accumulatedCyclesA = 0;
                }

                // Bit 4 always reads back zero
                timerAControl &= ~0x10;
                break;
            }
        case 0xDD0F: // Timer B Control
            {
                uint8_t oldValue = timerBControl;
                timerBControl = value;
                bool alarm = (value & 0x80) != 0; // Bit 7 toggles alarm
                todAlarmSetMode = alarm;
                if (alarm)
                {
                    todAlarmTriggered = false;
                }

                // Force load on bit 4
                if (value & 0x10)
                {
                    timerB = (timerBHighByte << 8) | timerBLowByte;
                }
                // Start rising edge reload
                if ((value & 0x01) && !(oldValue & 0x01))
                {
                    timerB = (timerBHighByte << 8) | timerBLowByte;
                    accumulatedCyclesB = 0;
                }
                // Store without load bit
                timerBControl &= ~0x10;
                break;
            }
        default:
            break;
    }
}

void CIA2::latchTODClock()
{
    todLatch[0] = todClock[0];
    todLatch[1] = todClock[1];
    todLatch[2] = todClock[2];
    todLatch[3] = todClock[3];
    todLatched = true;
}

uint32_t CIA2::calculatePrescaler(uint8_t clkSel)
{
    static constexpr uint32_t table[4] = { 1, 8, 64, 1024 };
    return table[clkSel & 0x03];
}

void CIA2::updateTimers(uint32_t cyclesElapsed)
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
}

void CIA2::updateTimerA(uint32_t cyclesElapsed)
{
    if (timerAControl & 0x01) // Timer A is active (start bit set)
    {
        // Calculate the clock prescaler based on the lower three bits of timerAControl.
        clkSelA = calculatePrescaler(timerAControl & 0x07);

        // Accumulate the cycles elapsed.
        accumulatedCyclesA += cyclesElapsed;

        // Determine how many timer ticks have occurred.
        uint32_t ticks = accumulatedCyclesA / clkSelA;

        // Retain any leftover cycles for the next update.
        accumulatedCyclesA %= clkSelA;

        if (ticks > 0)
        {
            if (timerA > ticks)
            {
                // Normal decrement: subtract the number of ticks from timerA.
                timerA -= ticks;
            }
            else
            {
                // Timer underflow occurs.
                timerAUnderFlowFlag = true;
                uint32_t extraTicks = ticks - timerA;

                // Reload timerA from its latch every time.
                timerA = (timerAHighByte << 8) | timerALowByte;

                if (timerAControl & 0x08)
                {
                    // one-shot mode: stop the timer after underflow
                    timerAControl &= ~0x01;
                }
                // else continuous mode: leave start bit set
                if (timerA > extraTicks)
                {
                    timerA -= extraTicks;
                }
                else
                {
                    timerA = 0;
                }
                // Trigger the Timer A interrupt.
                if (interruptEnable & INTERRUPT_TIMER_A)
                {
                    interruptStatus |= INTERRUPT_TIMER_A;
                    clearNMI();
                    triggerNMI();
                }
                if (timerAControl & 0x40)
                {
                    portB ^= DSR_MASK; // PB7
                    if (dataDirectionPortB & DSR_MASK && rs232dev)
                    {
                        rs232dev->setDSR((portB & DSR_MASK)!=0);
                    }
                }
            }
        }
    }
}

void CIA2::updateTimerB(uint32_t cyclesElapsed)
{
    if (!(timerBControl & 0x01))
    {
        return; // not running
    }

    uint8_t modeB = timerBControl & 0x60;
    switch (modeB)
    {
      case 0x00:  // CPU-clocked
        {
          uint32_t prescale = calculatePrescaler(timerBControl & 0x07);
          accumulatedCyclesB += cyclesElapsed;
          while (accumulatedCyclesB >= prescale)
          {
            accumulatedCyclesB -= prescale;
            decrementAndHandleTimerB();
          }
        }
        break;

      case 0x20:  // count on Timer A underflows only
        if (timerAUnderFlowFlag)
        {
            decrementAndHandleTimerB();
        }
        break;

      case 0x40:  // count on Timer A pulses only
        if (timerAPulseFlag)
        {
            decrementAndHandleTimerB();
        }
        break;

      case 0x60:  // count on both underflows & pulses
        if (timerAUnderFlowFlag || timerAPulseFlag)
        {
            decrementAndHandleTimerB();
        }
        break;
    }

    // Clear the A-flags now that B has used them
    timerAUnderFlowFlag = false;
    timerAPulseFlag      = false;
}

void CIA2::decrementAndHandleTimerB()
{
    if (timerB > 0)
    {
        --timerB;
    }
    else
    {
        if (timerBControl & 0x40) // bit 6 = toggle enable
        {
            portB ^= CTS_MASK; // CTS_MASK==0x40 == PB6
            if (dataDirectionPortB & CTS_MASK && rs232dev)
            {
                rs232dev->setCTS((portB & CTS_MASK) !=0);
            }
        }
        timerB = (timerBHighByte << 8) | timerBLowByte;
        if (timerBControl & 0x08)    // one-shot?
        {
            timerBControl &= ~0x01;  // stop
        }

        // toggle PB6 if requested
        if (interruptEnable & INTERRUPT_TIMER_B)
        {
            interruptStatus |= INTERRUPT_TIMER_B;
            clearNMI();
            triggerNMI();
        }
    }
}

void CIA2::incrementTODClock(uint32_t& todTicks, uint8_t todClock[], uint32_t todIncrementThreshold)
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

void CIA2::checkTODAlarm(uint8_t todClock[], const uint8_t todAlarm[], bool& todAlarmTriggered, uint8_t& interruptStatus, uint8_t interruptEnable)
{
    if (todClock[0] == todAlarm[0] && todClock[1] == todAlarm[1] &&
        todClock[2] == todAlarm[2] && todClock[3] == todAlarm[3])
    {
        if (!todAlarmTriggered)
        {
            todAlarmTriggered = true;
            if (interruptEnable & INTERRUPT_TOD_ALARM) // TOD interrupt enabled
            {
                interruptStatus |= INTERRUPT_TOD_ALARM;
                clearNMI();
                triggerNMI();
            }
        }
    }
    else
    {
        todAlarmTriggered = false; // Reset when TOD clock no longer matches alarm
    }
}

void CIA2::triggerNMI()
{
    if (!nmiAsserted && processor)
    {
        nmiAsserted = true;
        processor->executeNMI(); // Handle NMI
    }
}

void CIA2::clearNMI()
{
    nmiAsserted = false;
}

void CIA2::refreshNMI()
{
    if (interruptStatus & interruptEnable & 0x1F)
    {
        if (!nmiAsserted)
        {
            triggerNMI();
        }
    }
    else
    {
        clearNMI();
    }
}

void CIA2::clkChanged(bool level)
{
    if (lastClk & !level)
    {
        timerAPulseFlag = true;
        if (interruptEnable & INTERRUPT_SERIAL_SHIFT_REGISTER)
        {
            interruptStatus |= INTERRUPT_SERIAL_SHIFT_REGISTER;
            clearNMI();
            triggerNMI();
        }
        if (bus)
        {
            shiftReg = (shiftReg << 1) | (bus->readDataLine() ? 1 : 0);
            ++bitCount;
        }
        if (talking && !atnLine && bus)
        {
            // send bit [outBit] of serialDataRegister
            bool bit = (serialDataRegister >> outBit) & 1;
            bus->setDataLine(bit);

            // decrement & wrap bit index
            if (--outBit < 0)
                outBit = 7;
        }
    }
    if (bitCount == 8)
    {
        if(atnLine)
        {
            decodeIECCommand(shiftReg);
        }
        else
        {
            if (listening && !atnLine && currentSecondaryAddress == expectedSecondaryAddress)
            {
                serialDataRegister = shiftReg;
                // Tell the CPU to read the byte
                interruptStatus |= INTERRUPT_FLAG_LINE;
                if (interruptEnable & INTERRUPT_FLAG_LINE)
                {
                    clearNMI();
                    triggerNMI();
                }
            }
        }
        shiftReg = 0;
        bitCount = 0;
    }
    lastClk = level;
}

void CIA2::dataChanged(bool state)
{
    // on a falling DATA edge, if we're TALKing and not in ATN, push the next bit out
    if (lastDataLevel && !state && talking && !atnLine && bus)
    {
        // grab next bit from shiftReg (MSB first)
        bool bit = ((shiftReg >> outBit) & 1) != 0;
        bus->setDataLine(bit);
        if (--outBit < 0) outBit = 7;
    }
    lastDataLevel = state;
}

void CIA2::atnChanged(bool asserted)
{
    // Did we just go from high to low?
    bool fallingEdge = lastAtnLevel && !asserted;

    // Update the current ATN line level
    atnLine = asserted;

    if (fallingEdge)
    {
        // ATN was just asserted by the C64
        currentSecondaryAddress = 0xFF; // no address selected
        listening = false;
        talking = false;
        shiftReg = 0;
        bitCount = 0;
        outBit = 7;
    }

    // Remember for next time
    lastAtnLevel = asserted;
}

void CIA2::srqChanged(bool level)
{
    // detect falling edge of SRQ
    if (lastSrqLevel && !level)
    {
        // LISTEN mode: sample DATA into shiftReg
        if (listening && !atnLine && bus)
        {
            // read the IEC DATA line
            bool data = bus->readDataLine();
            // shift one bit in
            shiftReg = (shiftReg << 1) | (data ? 1 : 0);
            if (++bitCount == 8)
            {
                serialDataRegister = shiftReg;
                bitCount = 0;
                // trigger FLAG interrupt if enabled
                if (interruptEnable & INTERRUPT_SERIAL_SHIFT_REGISTER)
                {
                    interruptStatus |= INTERRUPT_SERIAL_SHIFT_REGISTER;
                    clearNMI();
                    triggerNMI();
                }
                shiftReg = 0;
            }
        }
        // TALK mode: drive DATA from serialDataRegister
        else if (talking && !atnLine && currentSecondaryAddress == expectedSecondaryAddress && bus)
        {
            // output bit outBitIndex of the byte
            bool out = (serialDataRegister >> outBit) & 1;
            bus->setDataLine(out);
            if (--outBit < 0)
                outBit = 7;
        }
    }

    lastSrqLevel = level;
}

void CIA2::decodeIECCommand(uint8_t cmd)
{
    uint8_t code = cmd & 0xF0;
    uint8_t device = cmd & 0x0F;
    if (bus)
    {
        switch (code)
        {
            case 0x20:  // LISTEN <device>
            {
                if (device == deviceNumber)
                {
                    bus->listen(device);
                    listening = true;
                    talking = false;
                    currentSecondaryAddress = 0xFF;
                    expectedSecondaryAddress = 0xFF;
                }
                break;
            }
            case 0x30:  // UNLISTEN <device>
            {
                if (device == deviceNumber)
                {
                    bus->unListen(device);
                    listening = false;
                    talking = false;
                    currentSecondaryAddress = 0xFF;
                    expectedSecondaryAddress = 0xFF;
                }
                break;
            }
            case 0x40:  // TALK <device>
            {
                if (device == deviceNumber)
                {
                    bus->talk(device);
                    talking = true;
                    listening = false;
                    currentSecondaryAddress = 0xFF;
                    expectedSecondaryAddress = 0xFF;
                    outBit = 7;
                }
                break;
            }
            case 0x60:  // UNTALK <device>
            {
                if (device == deviceNumber)
                {
                    bus->unTalk(device);
                    talking = false;
                    listening = false;
                    currentSecondaryAddress = 0xFF;
                    expectedSecondaryAddress = 0xFF;
                }
                break;
            }
            case 0xE0:  // secondary address (if you need it)
            {
                if (listening || talking)
                {
                    currentSecondaryAddress = device;
                    expectedSecondaryAddress = currentSecondaryAddress;
                }
                break;
            }
            default:
                // ignore and log unknown command
                if (logger)
                {
                    logger->WriteLog("Error: Unknown IEC Bus command encountered: " + std::to_string(code));
                }
                break;
        }
    }
}
