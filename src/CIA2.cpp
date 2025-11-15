// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "cia2.h"
#include "CPU.h"

CIA2::CIA2() :
    processor(nullptr),
    bus(nullptr),
    logger(nullptr),
    rs232dev(nullptr),
    traceMgr(nullptr),
    vicII(nullptr)
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
    timerASnap = 0;
    timerBSnap = 0;
    timerALatched = false;
    timerBLatched = false;

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

    // IEC
    deviceNumber = 0xFF;
    listening = false;
    talking = false;
    currentSecondaryAddress = 0xFF;
    expectedSecondaryAddress = 0xFF;
    atnLine = false;
    lastAtnLevel = false;
    lastClk = false;
    lastSrqLevel = false;
    lastDataLevel = true;
    shiftReg = 0;
    bitCount = 0;

    // CNT
    cntLevel = true;
    lastCNT = true;
    pendingTBCNTTicks = 0;
    pendingTBCASTicks = 0;

    // ML Monitor logging disable by default
    setLogging = false;
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
            // Base: outputs from latch, inputs = 1 (pull-ups)
            uint8_t result = (portA & dataDirectionPortA) | (~dataDirectionPortA & 0xFF);

            if (bus)
            {
                // PA6: CLK IN
                if (bus->readClkLine())
                    result |= MASK_CLK_IN;   // line high
                else
                    result &= ~MASK_CLK_IN;  // line low

                // PA7: DATA IN
                if (bus->readDataLine())
                    result |= MASK_DATA_IN;
                else
                    result &= ~MASK_DATA_IN;
            }

            //return result;
            //return portA;
            uint8_t value = portA;
            const uint8_t iec_mask = 0x38;

            // 2. Clear the old IEC bits from the value
            value &= ~iec_mask;

            // 3. Get the current PHYSICAL state of the IEC bus pins (1=HIGH, 0=LOW)
            uint8_t pin_states = 0;
            // The bus line functions must return the actual line state (HIGH/LOW)
            if (bus->getAtnLine())  pin_states |= MASK_ATN_OUT;
            if (bus->getClkLine())  pin_states |= MASK_CLK_OUT;
            if (bus->getDataLine()) pin_states |= MASK_DATA_OUT;

            // 4. Merge the physical pin states back into the value
            // This ensures the KERNAL reads the actual state of CLK/DATA/ATN.
            value |= (pin_states & iec_mask);

            return value;
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
            timerASnap = timerA;
            timerALatched = true;
            return timerASnap & 0xFF;
        case 0xDD05: // Timer A high byte
            //return timerAHighByte;
            if (timerALatched)
            {
                timerALatched = false;
                return timerASnap >> 8;
            }
            return (timerA >> 8) & 0xFF;
        case 0xDD06: // Timer B low byte
            timerBSnap = timerB;
            timerBLatched = true;
            return timerBSnap & 0xFF;
        case 0xDD07: // Timer B high byte
            if (timerBLatched)
            {
                timerBLatched = false;
                return timerBSnap >> 8;
            }
            return (timerB >> 8) & 0xFF;
        case 0xDD08: // TOD Clock 1/10 seconds
            if (!todLatched) latchTODClock();
            return binaryToBCD(todLatch[0]);
        case 0xDD09: // TOD Clock seconds
            if (!todLatched) latchTODClock();
            return binaryToBCD(todLatch[1]);
        case 0xDD0A: // TOD Clock minutes
            if (!todLatched) latchTODClock();
            return binaryToBCD(todLatch[2]);
        case 0xDD0B: // TOD Clock hours
            if (!todLatched) latchTODClock();
            todLatched = false; // Clear latch after reading hours
            return binaryToBCD(todLatch[3]);
        case 0xDD0C: // Serial data register
            return serialDataRegister;
        case 0xDD0D: // Interrupt control register
        {
            uint8_t result = interruptStatus & 0x1F; // IFR bits 0..4
            if (result & interruptEnable) result |= 0x80; // bit7 = any pending (UNMASKED)

            // Reading ICR *acks* currently-set IFR bits (0..4):
            interruptStatus &= ~(result & 0x1F);

            if (result & INTERRUPT_TOD_ALARM) todAlarmTriggered = false;
            refreshNMI();
            return result;
        }
        case 0xDD0E: // Timer A control
            return timerAControl & 0x7F;
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
             uint8_t oldPA = portA;
            // Merge new outputs only on DDR=1 bits; keep all DDR=0 bits (VIC bank) intact
            //portA = (portA & ~dataDirectionPortA) | (value &  dataDirectionPortA);
            portA = value;
            if (oldPA != portA) recomputeIEC();

            if (logger && setLogging)
            {
                std::stringstream out;
                out << "Updated Port A in CIA2 to: " << static_cast<int>(value) << " giving effective value: " << static_cast<int>(portA);
                logger->WriteLog(out.str());
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
            {
                dataDirectionPortA = value;
                recomputeIEC();
                break;
            }
        case 0xDD03: // Data direction port B
            dataDirectionPortB = value;
            break;
        case 0xDD04: // Timer A low byte
            timerALowByte = value;
            break;
        case 0xDD05: // Timer A high byte
            timerAHighByte = value;
            break;
        case 0xDD06: // Timer B low byte
            timerBLowByte = value;
            break;
        case 0xDD07: // Timer B high byte
            timerBHighByte = value;
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
                interruptEnable &= ~(value & 0x1F);
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

uint16_t CIA2::getCurrentVICBank() const
{
    // Inputs float high; outputs drive from portA latch.
    uint8_t effectivePA = (portA & dataDirectionPortA) | (~dataDirectionPortA);
    uint8_t bankBits    = (~effectivePA) & 0x03;    // PA1:PA0 inverted
    return static_cast<uint16_t>(bankBits) * 0x4000;
}

void CIA2::latchTODClock()
{
    todLatch[0] = todClock[0];
    todLatch[1] = todClock[1];
    todLatch[2] = todClock[2];
    todLatch[3] = todClock[3];
    todLatched = true;
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
    if (!(timerAControl & 0x01) || cyclesElapsed == 0) return;

    const bool cntMode = (timerAControl & 0x20) != 0;
    if (cntMode) return;

    while (cyclesElapsed--)
    {
        uint32_t cur = timerA ? timerA : 0x10000;
        if (--cur == 0)
        {
            timerA = (timerAHighByte << 8) | timerALowByte;
            if (timerAControl & 0x08) timerAControl &= ~0x01; // one-shot stop

            // IFR: TA always latches
            interruptStatus |= INTERRUPT_TIMER_A;
            refreshNMI();

            if (traceMgr && traceMgr->isEnabled() && traceMgr->catOn(TraceManager::TraceCat::CIA2))
            {
                TraceManager::Stamp stamp = traceMgr->makeStamp(processor ? processor->getTotalCycles() : 0, vicII ? vicII->getCurrentRaster() : 0,
                    vicII ? vicII->getRasterDot() : 0);
                traceMgr->recordCiaTimer(2, 'A', timerA, true, stamp);
                traceMgr->recordCiaICR(2, interruptStatus, nmiAsserted, stamp);
            }

            if (timerBControl & 0x40) ++pendingTBCASTicks;

            const bool pbOn  = (timerAControl & 0x02);
            const bool pulse = (timerAControl & 0x04);
            if (pbOn && (dataDirectionPortB & DSR_MASK))
            {
                if (pulse) { portB |= DSR_MASK; }
                else        { portB ^= DSR_MASK; }
                if (rs232dev) rs232dev->setDSR((portB & DSR_MASK)!=0);
            }
        }
        else
        {
            timerA = static_cast<uint16_t>(cur);
        }
    }
}

void CIA2::updateTimerB(uint32_t cyclesElapsed)
{
    // Not running?
    if ((timerBControl & 0x01) == 0)
        return;

    // Decode sources (6526-accurate)
    const bool cntSrc = (timerBControl & 0x20) != 0;  // CRB bit5: 1=CNT
    const bool casc   = (timerBControl & 0x40) != 0;  // CRB bit6: 1=CASCADE (dominates)

    if (casc) {
        // CASCADE: TB ticks once per TA underflow (CRB6 dominates; ignore CRB5)
        while (pendingTBCASTicks > 0)
        {
            --pendingTBCASTicks;
            tickTimerBOnce();   // one decrement + underflow handling
        }
        return;
    }

    if (cntSrc) {
        // CNT: TB ticks once per external CNT falling edge (when CRB5=1, CRB6=0)
        while (pendingTBCNTTicks > 0)
        {
            --pendingTBCNTTicks;
            tickTimerBOnce();
        }
        return;
    }

    // φ2: TB ticks once per CPU cycle (no prescaler on real 6526)
    while (cyclesElapsed-- > 0)
    {
        tickTimerBOnce();
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
            interruptStatus |= INTERRUPT_TOD_ALARM;
            refreshNMI();

            if (traceMgr && traceMgr->isEnabled() && traceMgr->catOn(TraceManager::TraceCat::CIA2))
            {
                TraceManager::Stamp stamp = traceMgr->makeStamp(processor ? processor->getTotalCycles() : 0, vicII ? vicII->getCurrentRaster() : 0,
                    vicII ? vicII->getRasterDot() : 0);
                traceMgr->recordCiaICR(2, interruptStatus, nmiAsserted, stamp);
            }
        }
    }
}

void CIA2::refreshNMI()
{
    // Drive the NMI line level based on (IFR & IER)
    bool level = (interruptStatus & interruptEnable & 0x1F) != 0;
    nmiAsserted = level;
    if (processor) processor->setNMILine(level);
}

void CIA2::clkChanged(bool level)
{
    const bool falling = (lastClk && !level);
    lastClk = level;

    if (!falling || !bus) return;

    // LISTEN receive (not under ATN): shift one bit in
    if (listening && !atnLine)
    {
        shiftReg = (shiftReg << 1) | (bus->readDataLine() ? 1 : 0);
        if (++bitCount == 8)
        {
            serialDataRegister = shiftReg;
            bitCount = 0;
            shiftReg = 0;

            // Byte ready -> SR (not FLAG); IFR latches unconditionally
            interruptStatus |= INTERRUPT_SERIAL_SHIFT_REGISTER;
            refreshNMI();
        }
    }

    // TALK transmit
    if (talking && !atnLine)
    {
        bool bit = (serialDataRegister >> outBit) & 1;
        bus->setDataLine(bit);
        if (--outBit < 0) outBit = 7;
    }
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

void CIA2::atnChanged(bool assertedLow)
{
    bool fallingEdge = !lastAtnLevel && assertedLow;

    atnLine = assertedLow;

    if (fallingEdge)
    {
        // ATN was just asserted by the C64
        currentSecondaryAddress = 0xFF;
        listening = false;
        talking  = false;
        shiftReg = 0;
        bitCount = 0;
        outBit   = 7;
    }

    lastAtnLevel = assertedLow;
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
                interruptStatus |= INTERRUPT_SERIAL_SHIFT_REGISTER;
                refreshNMI();
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
    uint8_t code   = cmd & 0xF0;
    uint8_t device = cmd & 0x0F;

    if (!bus)
        return;

    switch (code)
    {
        case 0x20:  // LISTEN <device>
        {
            bus->listen(device);
            listening  = true;
            talking    = false;
            currentSecondaryAddress  = 0xFF;
            expectedSecondaryAddress = 0xFF;
            break;
        }

        case 0x30:  // UNLISTEN <device>
        {
            bus->unListen(device);
            listening  = false;
            talking    = false;
            currentSecondaryAddress  = 0xFF;
            expectedSecondaryAddress = 0xFF;
            break;
        }

        case 0x40:  // TALK <device>
        {
            bus->talk(device);
            talking    = true;
            listening  = false;
            currentSecondaryAddress  = 0xFF;
            expectedSecondaryAddress = 0xFF;
            outBit = 7;
            break;
        }

        case 0x60:  // UNTALK <device>
        {
            bus->unTalk(device);
            talking    = false;
            listening  = false;
            currentSecondaryAddress  = 0xFF;
            expectedSecondaryAddress = 0xFF;
            break;
        }

        case 0xE0:  // secondary address
        {
            if (listening || talking)
            {
                currentSecondaryAddress  = device;
                expectedSecondaryAddress = currentSecondaryAddress;
            }
            break;
        }

        default:
        {
            if (logger && setLogging)
            {
                logger->WriteLog(
                    "Error: Unknown IEC Bus command encountered: " +
                    std::to_string(code));
            }
            break;
        }
    }
}

void CIA2::setCNTLine(bool level)
{
    bool falling = (lastCNT && !level);
    lastCNT = cntLevel = level;
    if (!falling) return;

    if ((timerBControl & 0x01) && (timerBControl & 0x20) && !(timerBControl & 0x40))
    {
        ++pendingTBCNTTicks; // updateTimerB() will consume these
    }
}

std::string CIA2::dumpRegisters(const std::string& group) const
{
    std::stringstream out;
    out << std::hex << std::uppercase << std::setfill('0');

    // Port registers
    if (group == "port" || group == "all")
    {
        out << "\nPort Registers \n\n";
        out << "PORT A = $" << std::setw(2) << int(portA) << "\n";
        out << "PORT A Data Direction Register = $" << std::setw(2) << int(dataDirectionPortA) << "\n";
        out << "PORT B = $" << std::setw(2) << int(portB) << "\n";
        out << "PORT B Data Direction Register = $" << std::setw(2) << int(dataDirectionPortB) << "\n";
    }

    // Timer registers
    if (group == "timer" || group == "all")
    {
        out << "\nTimer Registers \n\n";
        out << "Timer A Latch Low = $" << std::setw(2) << static_cast<int>(timerALowByte) << "\n";
        out << "Timer A Latch High = $" << std::setw(2) << static_cast<int>(timerAHighByte) << "\n";
        out << "Timer A Current = $" << std::setw(4) << timerA << "\n";
        out << "Timer A Control Register = $" << std::setw(2) << static_cast<int>(timerAControl) <<  " Active: " << ((timerAControl & 0x01) ? "Yes" : "No")
            << " Mode: " << ((timerAControl & 0x08) ? "One shot" : "Continuous") << "\n";
        out << "Timer B Latch Low = $" << std::setw(2) << static_cast<int>(timerBLowByte) << "\n";
        out << "Timer B Latch High = $" << std::setw(2) << static_cast<int>(timerBHighByte) << "\n";
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
        out << "NMI Asserted = " << (nmiAsserted ? "Yes" : "No") << "\n";
    }

    // Serial data register
    if (group == "serial" || group == "all")
    {
        out << "\nSerial Register\n\n";
        out << "Serial Data Register = $" << std::setw(2) << static_cast<int>(serialDataRegister) << "\n";
        out << "Output Bit = " << outBit << "\n";
    }

    // VIC Bank
    if (group == "vic" || group == "all")
    {
        out << "\nVIC-II Bank Control\n\n";
        out << "Current VIC Bank = $" << std::setw(4) << getCurrentVICBank() << "\n";
    }

    // IEC
    if (group == "iec" || group == "all")
    {
        out << "\nIEC Bus State\n\n";
        out << "Device Number = " << static_cast<int>(deviceNumber) << "\n";
        out << "Listening = " << (listening ? "Yes" : "No") << "\n";
        out << "Talking   = " << (talking ? "Yes" : "No") << "\n";
        out << "ATN Line  = " << (atnLine ? "Asserted" : "Released") << "\n";
    }

    return out.str();
}

void CIA2::tickTimerBOnce()
{
    uint32_t cur = timerB ? timerB : 0x10000;  // 0 represents 65536 in CIA timers
    if (--cur != 0)
    {
        timerB = static_cast<uint16_t>(cur);
        return;
    }

    handleTimerBUnderflow();
}

void CIA2::handleTimerBUnderflow()
{
    // Latch IFR regardless of IER; refresh NMI level from (IFR & IER)
    interruptStatus |= INTERRUPT_TIMER_B;
    refreshNMI();

    if (traceMgr && traceMgr->isEnabled() && traceMgr->catOn(TraceManager::TraceCat::CIA2))
    {
        TraceManager::Stamp stamp = traceMgr->makeStamp(processor ? processor->getTotalCycles() : 0, vicII ? vicII->getCurrentRaster() : 0,
            vicII ? vicII->getRasterDot() : 0);
        traceMgr->recordCiaTimer(2, 'B', timerA, true, stamp);
        traceMgr->recordCiaICR(2, interruptStatus, nmiAsserted, stamp);
    }

    const bool oneShot = (timerBControl & 0x08) != 0;  // RUNMODE=1 => one-shot
    if (oneShot)
    {
        timerB = 0;
        timerBControl &= ~0x01; // clear START
    }
    else
    {
        timerB = static_cast<uint16_t>((timerBHighByte << 8) | timerBLowByte);
    }

    // PB6 output (CTS) per PBON/OUTMODE; only when that bit is an output in DDRB
    const bool pbOn  = (timerBControl & 0x02) != 0; // PBON
    const bool pulse = (timerBControl & 0x04) != 0; // OUTMODE: 1=pulse, 0=toggle
    if (pbOn && (dataDirectionPortB & CTS_MASK))
    {
        if (pulse)
        {
            portB |= CTS_MASK;
        }
        else
        {
            portB ^= CTS_MASK; // toggle
        }
        if (rs232dev) rs232dev->setCTS((portB & CTS_MASK) != 0);
    }
}

void CIA2::setIERExact(uint8_t mask)
{
    mask &= 0x1F;
    interruptEnable = mask;
    refreshNMI();
}

void CIA2::recomputeIEC()
{
    if (!bus)
        return;

    uint8_t lowMask = dataDirectionPortA & portA;

    static uint8_t prevLowMask = 0xFF;
    if (lowMask != prevLowMask)
    {
        std::cout << "[CIA2] lowMask=$" << std::hex << int(lowMask)
                  << "  DDRA=$" << int(dataDirectionPortA)
                  << "  PA=$"   << int(portA) << std::dec << "\n";
        prevLowMask = lowMask;
    }

    bool atnLow  = (lowMask & MASK_ATN_OUT)  != 0;
    bool clkLow  = (lowMask & MASK_CLK_OUT)  != 0;
    bool dataLow = (lowMask & MASK_DATA_OUT) != 0;

    // Bus API: true = line HIGH (released), false = line LOW (asserted)
    bus->setAtnLine(atnLow);
    bus->setClkLine(clkLow);
    bus->setDataLine(dataLow);

    std::cout << "[CIA2] DDRA=" << std::hex << int(dataDirectionPortA)
              << " PA="   << int(portA)
              << " | ATNlow="  << atnLow
              << " CLKlow="    << clkLow
              << " DATAlow="   << dataLow << std::dec << "\n";
}
