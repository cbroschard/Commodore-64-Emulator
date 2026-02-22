// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Drive/DriveCIA.h"

DriveCIA::DriveCIA() :
    parentPeripheral(nullptr),
    portAPins(0xFF),
    portBPins(0xFF),
    cntLevel(true),
    lastCntLevel(true),
    flagLine(true),
    lastFlagLine(true),
    todAlarmSetMode(0),
    todAlarmTriggered(false),
    interruptStatus(0x00),
    lastAtnLow(false),
    extDataLow(false),
    autoAtnAckEnabled(false),
    ackArmed(false),
    lastClkInLowForAck(false),
    atnAckHoldCycles(0),
    atnAckArmedWhileClkLow(false),
    atnAckSawClkHigh(false),
    atnAckSawClkLow(false),
    iecAtnInLow(false),
    iecClkInLow(false),
    iecDataInLow(false)
{

}

DriveCIA::~DriveCIA() = default;

void DriveCIA::saveState(StateWriter& wrtr) const
{
    // Header / version
    wrtr.writeU32(1);

    wrtr.writeU8(registers.portA);
    wrtr.writeU8(registers.portB);
    wrtr.writeU8(registers.ddrA);
    wrtr.writeU8(registers.ddrB);

    wrtr.writeU8(registers.timerALowByte);
    wrtr.writeU8(registers.timerAHighByte);
    wrtr.writeU8(registers.timerBLowByte);
    wrtr.writeU8(registers.timerBHighByte);

    wrtr.writeU8(registers.tod10th);
    wrtr.writeU8(registers.todSeconds);
    wrtr.writeU8(registers.todMinutes);
    wrtr.writeU8(registers.todHours);

    wrtr.writeU8(registers.serialData);
    wrtr.writeU8(registers.interruptEnable);
    wrtr.writeU8(registers.controlRegisterA);
    wrtr.writeU8(registers.controlRegisterB);

    wrtr.writeU8(portAPins);
    wrtr.writeU8(portBPins);

    wrtr.writeBool(cntLevel);
    wrtr.writeBool(lastCntLevel);
    wrtr.writeBool(flagLine);
    wrtr.writeBool(lastFlagLine);

    wrtr.writeU16(timerACounter);
    wrtr.writeU16(timerALatch);
    wrtr.writeU16(timerBCounter);
    wrtr.writeU16(timerBLatch);
    wrtr.writeBool(timerARunning);
    wrtr.writeBool(timerBRunning);

    wrtr.writeU8(todAlarm10th);
    wrtr.writeU8(todAlarmSeconds);
    wrtr.writeU8(todAlarmMinutes);
    wrtr.writeU8(todAlarmHours);
    wrtr.writeU8(todAlarmSetMode);
    wrtr.writeBool(todAlarmTriggered);

    wrtr.writeU8(interruptStatus);

    wrtr.writeBool(lastAtnLow);
    wrtr.writeBool(extDataLow);
    wrtr.writeBool(autoAtnAckEnabled);
    wrtr.writeBool(ackArmed);
    wrtr.writeBool(lastClkInLowForAck);
    wrtr.writeU16(atnAckHoldCycles);
    wrtr.writeBool(atnAckArmedWhileClkLow);
    wrtr.writeBool(atnAckSawClkHigh);
    wrtr.writeBool(atnAckSawClkLow);

    wrtr.writeBool(iecAtnInLow);
    wrtr.writeBool(iecClkInLow);
    wrtr.writeBool(iecDataInLow);
}

bool DriveCIA::loadState(StateReader& rdr)
{
    // Header / version
    uint32_t ver = 0;
    if (!rdr.readU32(ver)) return false;
    if (ver != 1) return false;

    // Registers
    if (!rdr.readU8(registers.portA)) return false;
    if (!rdr.readU8(registers.portB)) return false;
    if (!rdr.readU8(registers.ddrA)) return false;
    if (!rdr.readU8(registers.ddrB)) return false;

    if (!rdr.readU8(registers.timerALowByte)) return false;
    if (!rdr.readU8(registers.timerAHighByte)) return false;
    if (!rdr.readU8(registers.timerBLowByte)) return false;
    if (!rdr.readU8(registers.timerBHighByte)) return false;

    if (!rdr.readU8(registers.tod10th)) return false;
    if (!rdr.readU8(registers.todSeconds)) return false;
    if (!rdr.readU8(registers.todMinutes)) return false;
    if (!rdr.readU8(registers.todHours)) return false;

    if (!rdr.readU8(registers.serialData)) return false;
    if (!rdr.readU8(registers.interruptEnable)) return false;
    if (!rdr.readU8(registers.controlRegisterA)) return false;
    if (!rdr.readU8(registers.controlRegisterB)) return false;

    // Pins
    if (!rdr.readU8(portAPins)) return false;
    if (!rdr.readU8(portBPins)) return false;

    // CNT + FLAG
    if (!rdr.readBool(cntLevel)) return false;
    if (!rdr.readBool(lastCntLevel)) return false;
    if (!rdr.readBool(flagLine)) return false;
    if (!rdr.readBool(lastFlagLine)) return false;

    // Timers
    if (!rdr.readU16(timerACounter)) return false;
    if (!rdr.readU16(timerALatch)) return false;
    if (!rdr.readU16(timerBCounter)) return false;
    if (!rdr.readU16(timerBLatch)) return false;
    if (!rdr.readBool(timerARunning)) return false;
    if (!rdr.readBool(timerBRunning)) return false;

    // TOD Alarm
    if (!rdr.readU8(todAlarm10th)) return false;
    if (!rdr.readU8(todAlarmSeconds)) return false;
    if (!rdr.readU8(todAlarmMinutes)) return false;
    if (!rdr.readU8(todAlarmHours)) return false;
    if (!rdr.readU8(todAlarmSetMode)) return false;
    if (!rdr.readBool(todAlarmTriggered)) return false;

    // Interrupt latch
    if (!rdr.readU8(interruptStatus)) return false;

    // ATN auto-ack handshake
    if (!rdr.readBool(lastAtnLow)) return false;
    if (!rdr.readBool(extDataLow)) return false;
    if (!rdr.readBool(autoAtnAckEnabled)) return false;
    if (!rdr.readBool(ackArmed)) return false;
    if (!rdr.readBool(lastClkInLowForAck)) return false;
    if (!rdr.readU16(atnAckHoldCycles)) return false;
    if (!rdr.readBool(atnAckArmedWhileClkLow)) return false;
    if (!rdr.readBool(atnAckSawClkHigh)) return false;
    if (!rdr.readBool(atnAckSawClkLow)) return false;

    // IEC inputs
    if (!rdr.readBool(iecAtnInLow)) return false;
    if (!rdr.readBool(iecClkInLow)) return false;
    if (!rdr.readBool(iecDataInLow)) return false;

    // Post-restore fixups

    // Ensure the master IRQ flag bit matches whether any source bits are set
    if ((interruptStatus & 0x1F) == 0)
        interruptStatus &= 0x7F;
    else
        interruptStatus |= 0x80;

    // Overlay IEC inputs onto portBPins (so ROM sees correct ATN/CLK/DATA inputs)
    applyIECInputsToPortBPins();

    // Re-apply outputs based on restored registers/DDRs (safe even if no bus attached)
    applyPortOutputs();

    applyIECOutputs();

    return true;
}

void DriveCIA::reset()
{
    // Initialize all registers
    registers.portA             = 0x00;
    registers.portB             = 0x00;
    registers.ddrA              = 0x00;
    registers.ddrB              = 0x00;
    registers.timerALowByte     = 0x00;
    registers.timerAHighByte    = 0x00;
    registers.timerBLowByte     = 0x00;
    registers.timerBHighByte    = 0x00;
    registers.tod10th           = 0x00;
    registers.todSeconds        = 0x00;
    registers.todMinutes        = 0x00;
    registers.todHours          = 0x00;
    registers.serialData        = 0x00;
    registers.interruptEnable   = 0x00;
    registers.controlRegisterA  = 0x00;
    registers.controlRegisterB  = 0x00;

    // Port pins
    portAPins                   = 0xFF;
    portBPins                   = 0xFF;

    cntLevel                    = true;
    lastCntLevel                = true;

    flagLine                    = true;
    lastFlagLine                = true;

    // IRQ reset
    interruptStatus = 0x00;

    // Reset alarms
    todAlarm10th                = 0x00;
    todAlarmSeconds             = 0x00;
    todAlarmMinutes             = 0x00;
    todAlarmHours               = 0x00;
    todAlarmSetMode             = 0x00;
    todAlarmTriggered           = false;

    // Reset timers
    timerACounter               = 0;
    timerALatch                 = 0;
    timerARunning               = false;
    timerBCounter               = 0;
    timerBLatch                 = 0;
    timerBRunning               = false;

    // BUS side
    iecAtnInLow                 = false;
    iecClkInLow                 = false;
    iecDataInLow                = false;

    // Handshake
    lastAtnLow                  = false;
    extDataLow                  = false;
    ackArmed                    = false;
    lastClkInLowForAck          = iecClkInLow;
    atnAckHoldCycles            = 0;
    atnAckArmedWhileClkLow      = false;
    atnAckSawClkHigh            = false;
    atnAckSawClkLow             = false;
}

void DriveCIA::notifyAtnInput(bool atnLow)
{
    #ifdef Debug
    std::cout << "[CIA] notifyAtnInput atnLow=" << atnLow
              << " forcedAutoAck=" << autoAtnAckEnabled
              << " lastAtnLow=" << lastAtnLow
              << " ext(before)=" << extDataLow << "\n";
    #endif

    const uint8_t ddrB  = registers.ddrB;
    const uint8_t portB = registers.portB;

    const bool hwAutoAtnRespEnable =
        ((ddrB & PRB_ATNACK) != 0) && ((portB & PRB_ATNACK) != 0);

    const bool autoAckEnabled = autoAtnAckEnabled || hwAutoAtnRespEnable;

    const bool falling = (!lastAtnLow &&  atnLow);
    const bool rising  = ( lastAtnLow && !atnLow);

    if (!autoAckEnabled)
    {
        lastAtnLow = atnLow;
        return;
    }

    if (falling)
    {
        ackArmed = true;

        // IMPORTANT: if CLK is already low at the moment ATN falls,
        // we must treat that as "saw CLK low" immediately.
        atnAckSawClkLow  = iecClkInLow;
        atnAckSawClkHigh = false;

        lastClkInLowForAck = iecClkInLow;
        atnAckHoldCycles = 0;

        // If CLK already low, assert ACK now (otherwise tick() will assert when CLK drops)
        extDataLow = atnAckSawClkLow;

        applyIECOutputs();

        #ifdef Debug
        std::cout << "[CIA] ATN FALL: clkAlreadyLow=" << (iecClkInLow ? 1 : 0)
                  << " -> extDataLow=" << (extDataLow ? 1 : 0) << "\n";
        #endif
    }
    else if (rising)
    {
        #ifdef Debug
        std::cout << "[CIA] notifyAtnInput RISE (cancel ACK)\n";
        #endif
        ackArmed = false;
        extDataLow = false;
        applyIECOutputs();
    }

    lastAtnLow = atnLow;
}

void DriveCIA::tick(uint32_t cycles)
{
    while(cycles-- > 0)
    {
        // Update Pin B
        updatePinsFromBus();

        const bool hwAutoAtnRespEnable =
            (registers.ddrB & PRB_ATNACK) && ((registers.portB & PRB_ATNACK) != 0);
        const bool autoAckEnabled = autoAtnAckEnabled || hwAutoAtnRespEnable;

        if (autoAckEnabled && ackArmed)
        {
            const bool atnInLow = ((portBPins & PRB_ATNIN) != 0); // true when physical ATN is LOW
            const bool clkInLow = ((portBPins & PRB_CLKIN) != 0); // true when physical CLK is LOW

            // ATN released -> cancel immediately
            if (!atnInLow)
            {
                ackArmed = false;
                extDataLow = false;
                atnAckHoldCycles = 0;
                atnAckSawClkLow = false;
                atnAckSawClkHigh = false;
                applyIECOutputs();
            }
            else
            {
                // Detect edges on CLK (remember: clkInLow==true means physical CLK is LOW)
                const bool prevClkLow = lastClkInLowForAck;

                // High -> Low transition: controller pulled CLK low
                if (!prevClkLow && clkInLow)
                    atnAckSawClkLow = true;

                // Low -> High transition: controller released CLK high
                if (prevClkLow && !clkInLow)
                {
                    // Only count "saw high" once we've seen low at least once
                    if (atnAckSawClkLow)
                        atnAckSawClkHigh = true;
                }

                lastClkInLowForAck = clkInLow;

                // RELEASE ONLY AFTER: held long enough + saw low + then saw high
                if (atnAckHoldCycles >= MIN_ACK_HOLD && atnAckSawClkLow && atnAckSawClkHigh)
                {
                    ackArmed = false;
                    extDataLow = false;
                    applyIECOutputs();
                }

                // If ACK is asserted, count hold cycles
                if (extDataLow)
                {
                    ++atnAckHoldCycles;

                    // Release only after:
                    //  - minimum hold time
                    //  - saw CLK low and then saw CLK return high (full phase)
                    if (atnAckHoldCycles >= MIN_ACK_HOLD && atnAckSawClkLow && atnAckSawClkHigh)
                    {
                        ackArmed = false;
                        extDataLow = false;
                        applyIECOutputs();
                    }
                }
            }
        }

        // inside each cycle
        bool timerAUnderflowThisCycle = false;

        // --- Timer A decrement decision ---
        bool decA = false;
        if (timerARunning)
        {
            const uint8_t cra = registers.controlRegisterA;
            const bool countCNT = (cra & CRA_INMODE) != 0;

            if (!countCNT)
                decA = true;
            else
                decA = (cntLevel && !lastCntLevel); // CNT rising edge
        }

        // --- Timer A run ---
        if (decA && timerACounter > 0)
        {
            --timerACounter;
            registers.timerALowByte  = static_cast<uint8_t>(timerACounter & 0xFF);
            registers.timerAHighByte = static_cast<uint8_t>((timerACounter >> 8) & 0xFF);

            if (timerACounter == 0)
            {
                triggerInterrupt(INTERRUPT_TIMER_A);
                timerAUnderflowThisCycle = true;

                // one-shot?
                if (registers.controlRegisterA & CRA_RUNMODE)
                {
                    timerARunning = false;
                    registers.controlRegisterA &= static_cast<uint8_t>(~CRA_START);
                }
                else
                {
                    timerACounter = timerALatch;
                }
            }
        }

        // --- Timer B decrement decision ---
        bool decB = false;
        if (timerBRunning)
        {
            const uint8_t crb = registers.controlRegisterB;
            const uint8_t mode = crb & CRB_INMODE_MASK;

            if (mode == CRB_INMODE_PHI2) decB = true;
            else if (mode == CRB_INMODE_CNT) decB = (cntLevel && !lastCntLevel);
            else if (mode == CRB_INMODE_TA) decB = timerAUnderflowThisCycle;
            else /* TA underflow while CNT high */ decB = timerAUnderflowThisCycle && cntLevel;
        }

        // --- Timer B run ---
        if (decB && timerBCounter > 0)
        {
            --timerBCounter;
            registers.timerBLowByte  = static_cast<uint8_t>(timerBCounter & 0xFF);
            registers.timerBHighByte = static_cast<uint8_t>((timerBCounter >> 8) & 0xFF);

            if (timerBCounter == 0)
            {
                triggerInterrupt(INTERRUPT_TIMER_B);

                if (registers.controlRegisterB & CRB_RUNMODE)
                {
                    timerBRunning = false;
                    registers.controlRegisterB &= static_cast<uint8_t>(~CRB_START);
                }
                else
                {
                    timerBCounter = timerBLatch;
                }
            }
        }

        // end-of-cycle
        lastCntLevel = cntLevel;
    }
}

uint8_t DriveCIA::readRegister(uint16_t address)
{
    switch(address)
    {
        case 0x00:
        {
            uint8_t result = (registers.portA & registers.ddrA) | (portAPins & ~registers.ddrA);
            return result;
        }
        case 0x01:
        {
            uint8_t result = (registers.portB & registers.ddrB) | (portBPins & ~registers.ddrB);
            return result;
        }
        case 0x02: return registers.ddrA;
        case 0x03: return registers.ddrB;
        case 0x04: return registers.timerALowByte;
        case 0x05: return registers.timerAHighByte;
        case 0x06: return registers.timerBLowByte;
        case 0x07: return registers.timerBHighByte;
        case 0x08: return registers.tod10th;
        case 0x09: return registers.todSeconds;
        case 0x0A: return registers.todMinutes;
        case 0x0B: return registers.todHours;
        case 0x0C: return registers.serialData;
        case 0x0D:
        {
            uint8_t pending = interruptStatus & 0x1F;
            uint8_t result = pending;
            if (pending & registers.interruptEnable) result |= 0x80;

            // Clear the acknowledged sources (bits 0–4 that were set)
            interruptStatus &= static_cast<uint8_t>(~pending);
            if ((interruptStatus & 0x1F) == 0) interruptStatus &= 0x7F;
            return result;
        }
        case 0x0E: return registers.controlRegisterA;
        case 0x0F: return registers.controlRegisterB;
        default: return 0xFF;
    }
    return 0xFF;
}

void DriveCIA::writeRegister(uint16_t address, uint8_t value)
{
    switch(address)
    {
        case 0x00:
        {
            registers.portA = value;
            applyPortOutputs();
            break;
        }
        case 0x01:
        {
            registers.portB = value;
            applyPortOutputs();
            break;
        }
        case 0x02:
        {
            registers.ddrA = value;
            applyPortOutputs();
            break;
        }
        case 0x03:
        {
            registers.ddrB = value;
            applyPortOutputs();
            break;
        }
        case 0x04:
        {
            registers.timerALowByte = value;
            timerALatch = (timerALatch & 0xFF00) | value;
            // If stopped, CIA-style: keep counter reflecting latch
            if (!timerARunning) timerACounter = (timerACounter & 0xFF00) | value;
            break;
        }
        case 0x05:
        {
            registers.timerAHighByte = value;
            timerALatch = (static_cast<uint16_t>(registers.timerAHighByte) << 8) |
                          static_cast<uint16_t>(registers.timerALowByte);
            if (!timerARunning) timerACounter = timerALatch;
            break;
        }
        case 0x06:
        {
            registers.timerBLowByte = value;
            timerBLatch = (timerBLatch & 0xFF00) | value;
            if (!timerBRunning) timerBCounter = (timerBCounter & 0xFF00) | value;
            break;
        }
        case 0x07:
        {
            registers.timerBHighByte = value;
            timerBLatch = (static_cast<uint16_t>(registers.timerBHighByte) << 8) |
                          static_cast<uint16_t>(registers.timerBLowByte);
            if (!timerBRunning) timerBCounter = timerBLatch;
            break;
        }
        case 0x08: registers.tod10th = value; break;
        case 0x09: registers.todSeconds = value; break;
        case 0x0A: registers.todMinutes = value; break;
        case 0x0B: registers.todHours = value; break;
        case 0x0C: registers.serialData = value; break;
        case 0x0D:
        {
            uint8_t mask = value & 0x1F;
            if (value & 0x80)
            {
                registers.interruptEnable |= mask;
            }
            else
            {
                registers.interruptEnable &= static_cast<uint8_t>(~mask);
            }
            break;
        }
        case 0x0E: // CRA
        {
            const uint8_t old = registers.controlRegisterA;
            registers.controlRegisterA = value;

            const bool oldStart = (old & CRA_START) != 0;
            const bool newStart = (value & CRA_START) != 0;

            // Start transition: load counter from latch
            if (!oldStart && newStart)
                timerACounter = timerALatch;

            timerARunning = newStart;

            // FORCE LOAD (strobe): load latch into counter, then clear the bit
            if (value & CRA_LOAD)
            {
                timerACounter = timerALatch;
                registers.controlRegisterA &= static_cast<uint8_t>(~CRA_LOAD);
            }
            break;
        }

        case 0x0F: // CRB
        {
            const uint8_t old = registers.controlRegisterB;
            registers.controlRegisterB = value;

            const bool oldStart = (old & CRB_START) != 0;
            const bool newStart = (value & CRB_START) != 0;

            if (!oldStart && newStart)
                timerBCounter = timerBLatch;

            timerBRunning = newStart;

            if (value & CRB_LOAD)
            {
                timerBCounter = timerBLatch;
                registers.controlRegisterB &= static_cast<uint8_t>(~CRB_LOAD);
            }
            break;
        }
        default: break;
    }
}

void DriveCIA::triggerInterrupt(InterruptBit bit)
{
    interruptStatus |= (static_cast<uint8_t>(bit) & 0x1F);
    interruptStatus |= 0x80; // master IRQ flag
}

void DriveCIA::setFlagLine(bool level)
{
    flagLine = level;

    // Falling edge triggers FLAG interrupt (common convention)
    if (lastFlagLine && !flagLine)
        triggerInterrupt(INTERRUPT_FLAG_LINE);

    lastFlagLine = flagLine;
}

void DriveCIA::updatePinsFromBus()
{
    portAPins = 0xFF;
    portBPins = 0xFF;

    auto* drive = dynamic_cast<Drive*>(parentPeripheral);
    if (!drive) return;

    if (wiring && wiring->samplePortAPins)
        wiring->samplePortAPins(*this, *drive, portAPins);

    if (wiring && wiring->samplePortBPins)
        wiring->samplePortBPins(*this, *drive, portBPins);

    // Always overlay the real IEC input levels onto the ATN/CLK/DATA input bits
    // so the ROM sees correct inputs regardless of wiring callbacks.
    applyIECInputsToPortBPins();
}

void DriveCIA::applyIECOutputs()
{
    auto* drive = dynamic_cast<Drive*>(parentPeripheral);
    if (!drive) return;

    const uint8_t ddrB  = registers.ddrB;
    const uint8_t portB = registers.portB;

    // 1581 IEC outputs go through open-collector inverters (7406).
    // CIA output bit = 1  => IEC line asserted LOW
    // CIA output bit = 0  => IEC line released (HIGH via pullups)
    const bool datOutAssertLow = ((ddrB & PRB_DATOUT) != 0) && ((portB & PRB_DATOUT) != 0);
    const bool clkOutAssertLow = ((ddrB & PRB_CLKOUT) != 0) && ((portB & PRB_CLKOUT) != 0);

    // PB4 on a 1581 is NOT a direct IEC driver.
    // It enables the "automatic ATN response" logic.
    const bool hwAutoAtnRespEnable = ((ddrB & PRB_ATNACK) != 0) && ((portB & PRB_ATNACK) != 0);

    // Effective auto-ACK: either forced by emulator OR enabled by the ROM via PB4.
    const bool autoAckEnabled = autoAtnAckEnabled || hwAutoAtnRespEnable;

    const bool ciaDataLow   = datOutAssertLow;
    const bool ciaClkLow    = clkOutAssertLow;

    // External auto-ACK pulls DATA low when enabled.
    const bool busdirOut = ((ddrB & PRB_BUSDIR) != 0) && ((portB & PRB_BUSDIR) != 0);

    // Presence ACK ignores BUSDIR (hardware quirk)
    const bool autoAckDriveLow = autoAckEnabled && extDataLow;

    // Normal CIA output obeys BUSDIR
    const bool ciaDriveLow = busdirOut && ciaDataLow;

    const bool driveDataLow = autoAckDriveLow || ciaDriveLow;
    const bool driveClkLow  = ciaClkLow;

    drive->peripheralAssertData(driveDataLow);
    drive->peripheralAssertClk(driveClkLow);

    #ifdef Debug
    std::cout << "[CIA] applyIECOutputs: extDataLow=" << (extDataLow ? 1 : 0)
              << " autoAckEnabled=" << (autoAckEnabled ? 1 : 0)
              << " ciaDataLow=" << (ciaDataLow ? 1 : 0)
              << " -> driveDataLow=" << (driveDataLow ? 1 : 0)
              << "  driveClkLow=" << (driveClkLow ? 1 : 0)
              << "  ddrB=" << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
              << static_cast<int>(ddrB)
              << " portB=" << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
              << static_cast<int>(portB)
              << std::dec << "\n";
    #endif
}

void DriveCIA::applyPortOutputs()
{
    auto* drive = dynamic_cast<Drive*>(parentPeripheral);
    if (!drive || !wiring) return;

    if (wiring->applyPortAOutputs)
        wiring->applyPortAOutputs(*this, *drive, registers.portA, registers.ddrA);

    if (wiring->applyPortBOutputs)
        wiring->applyPortBOutputs(*this, *drive, registers.portB, registers.ddrB);

    applyIECOutputs();
}

void DriveCIA::setIECInputs(bool atnLow, bool clkLow, bool dataLow)
{
    #ifdef Debug
    std::cout << "[CIA] setIECInputs: ATN=" << atnLow
              << " CLK=" << clkLow
              << " DATA=" << dataLow
              << " (prev ATN=" << iecAtnInLow << ")\n";
    #endif
    const bool atnChanged = (atnLow != iecAtnInLow);

    iecAtnInLow  = atnLow;
    iecClkInLow  = clkLow;
    iecDataInLow = dataLow;

    // Keep port B input pins in sync immediately so ROM reads are correct.
    applyIECInputsToPortBPins();

    // Let the ATN-ACK state machine see the edge without requiring external callers
    // to manually call notifyAtnInput().
    if (atnChanged)
        notifyAtnInput(atnLow);
}

void DriveCIA::applyIECInputsToPortBPins()
{
    // 1581 hardware: IEC inputs go through an inverter (74LS14)
    // IEC LOW  => CIA pin HIGH => bit = 1
    // IEC HIGH => CIA pin LOW  => bit = 0

    if (iecAtnInLow)  portBPins |=  PRB_ATNIN;  else portBPins &= ~PRB_ATNIN;
    if (iecClkInLow)  portBPins |=  PRB_CLKIN;  else portBPins &= ~PRB_CLKIN;
    if (iecDataInLow) portBPins |=  PRB_DATAIN; else portBPins &= ~PRB_DATAIN;
}

void DriveCIA::primeAtnLevel(bool atnLow)
{
    #ifdef Debug
    std::cout << "[CIA] primeAtnLevel called, forcing extDataLow=0\n";
    #endif
    lastAtnLow = atnLow;     // sync edge detector baseline
    ackArmed = false;
    extDataLow = false;      // start released
    atnAckHoldCycles = 0;
    atnAckSawClkHigh = false;
    atnAckSawClkLow  = false;
    lastClkInLowForAck = iecClkInLow;
}
