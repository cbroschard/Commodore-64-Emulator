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
    iecAtnInLow(false),
    iecClkInLow(false),
    iecDataInLow(false),
    spLevel(false),
    lastSpLevel(false),
    serialShiftRegister(0x00),
    serialBitCount(0),
    serialRxArmed(false),
    serialRxJustArmed(false)
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

    wrtr.writeBool(iecAtnInLow);
    wrtr.writeBool(iecClkInLow);
    wrtr.writeBool(iecDataInLow);

    wrtr.writeBool(spLevel);
    wrtr.writeBool(lastSpLevel);
    wrtr.writeU8(serialShiftRegister);
    wrtr.writeU8(serialBitCount);
    wrtr.writeBool(serialRxArmed);
    wrtr.writeBool(serialRxJustArmed);
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

    // ATN Edge Tracking
    if (!rdr.readBool(lastAtnLow)) return false;

    // IEC inputs
    if (!rdr.readBool(iecAtnInLow)) return false;
    if (!rdr.readBool(iecClkInLow)) return false;
    if (!rdr.readBool(iecDataInLow)) return false;

    if (!rdr.readBool(spLevel)) return false;
    if (!rdr.readBool(lastSpLevel)) return false;
    if (!rdr.readU8(serialShiftRegister)) return false;
    if (!rdr.readU8(serialBitCount)) return false;
    if (!rdr.readBool(serialRxArmed)) return false;
    if (!rdr.readBool(serialRxJustArmed)) return false;

    // Post-restore fixups

    // Ensure the master IRQ flag bit matches whether any source bits are set
    if ((interruptStatus & 0x1F) == 0)
        interruptStatus &= 0x7F;
    else
        interruptStatus |= 0x80;

    // Overlay IEC inputs onto portBPins (so ROM sees correct ATN/CLK/DATA inputs)
    applyIECInputsToPortBPins();

    cntLevel = iecClkInLow;
    lastCntLevel = cntLevel;

    spLevel = iecDataInLow;
    lastSpLevel = spLevel;

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

    // CIA serial receive side
    spLevel                     = false;
    lastSpLevel                 = false;
    serialShiftRegister         = 0x00;
    serialBitCount              = 0;
    serialRxArmed               = false;
    serialRxJustArmed           = false;

    // ATN edge tracking
    lastAtnLow                  = false;
}

void DriveCIA::notifyAtnInput(bool atnLow)
{
#ifdef Debug
    std::cout << "[CIA] notifyAtnInput atnLow=" << atnLow
              << " lastAtnLow=" << lastAtnLow << "\n";
#endif

    const bool falling = (!lastAtnLow &&  atnLow);
    const bool rising  = ( lastAtnLow && !atnLow);

    if (falling)
    {
        // ATN asserted by host. Hardware path should notify the ROM
        // through the CIA interrupt/FLAG-style input path.
        setFlagLine(false);
    }
    else if (rising)
    {
        setFlagLine(true);
    }

    lastAtnLow = atnLow;

    // ATN level may affect the hardware ATN-ACK output gate.
    applyIECOutputs();
}

void DriveCIA::tick(uint32_t cycles)
{
    while (cycles-- > 0)
    {
        // Update Pin B
        updatePinsFromBus();

        // CIA serial receive path
        const bool cntRisingEdge = (cntLevel && !lastCntLevel);
        const bool sdrInputMode = (registers.controlRegisterA & CRA_SPMODE) == 0;

        // Do not sample in the same cycle the receiver was just armed.
        if (serialRxArmed && !serialRxJustArmed && sdrInputMode && cntRisingEdge)
        {
            serialShiftRegister = static_cast<uint8_t>((serialShiftRegister >> 1) | (spLevel ? 0x80 : 0x00));

            ++serialBitCount;

            #ifdef Debug
            std::cout << "[CIA] SDR bit: sp=" << (spLevel ? 1 : 0)
                      << " count=" << static_cast<int>(serialBitCount)
                      << " shift=$"
                      << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
                      << static_cast<int>(serialShiftRegister)
                      << std::dec << "\n";
            #endif

            if (serialBitCount == 8)
            {
                registers.serialData = serialShiftRegister;
                triggerInterrupt(INTERRUPT_SERIAL_SHIFT_REGISTER);

                #ifdef Debug
                std::cout << "[CIA] SDR RX complete: $"
                          << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
                          << static_cast<int>(registers.serialData)
                          << std::dec << "\n";
                #endif

                serialShiftRegister = 0x00;
                serialBitCount = 0;
                serialRxArmed = false;
                serialRxJustArmed = false;
            }
        }

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

        // CIA serial output path.
        // CRA bit 6 = 1 means serial output mode. In this mode Timer A underflows
        // clock bits out through the serial port. After 8 bits, ICR bit $08 is set.
        const bool sdrOutputMode = (registers.controlRegisterA & CRA_SPMODE) != 0;

        if (sdrOutputMode && timerAUnderflowThisCycle && !serialRxJustArmed)
        {
            // Minimal output emulation: advance one bit per Timer A underflow.
            // Exact SP/CNT output pin behavior can be improved later; the ROM's
            // immediate need here is the SDR-complete interrupt.
            ++serialBitCount;

            serialShiftRegister = static_cast<uint8_t>(serialShiftRegister << 1);

            if (serialBitCount >= 8)
            {
                triggerInterrupt(INTERRUPT_SERIAL_SHIFT_REGISTER);

                serialBitCount = 0;
                serialShiftRegister = registers.serialData;
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
            else decB = timerAUnderflowThisCycle && cntLevel;
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

        serialRxJustArmed = false;

        lastCntLevel = cntLevel;
        lastSpLevel = spLevel;
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
        case 0x0C:
        {
        #ifdef Debug
            std::cout << "[CIA] read SDR -> $"
                      << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
                      << static_cast<int>(registers.serialData)
                      << std::dec << "\n";
        #endif
            return registers.serialData;
        }
        case 0x0D:
        {
            uint8_t pending = interruptStatus & 0x1F;
            uint8_t result = pending;
            if (pending & registers.interruptEnable) result |= 0x80;

            interruptStatus &= static_cast<uint8_t>(~pending);
            if ((interruptStatus & 0x1F) == 0) interruptStatus &= 0x7F;

            if (auto* drive = dynamic_cast<Drive*>(parentPeripheral))
                drive->updateIRQ();

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

        #ifdef Debug
            std::cout << "[CIA] write PRB=$"
                      << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
                      << static_cast<int>(value)
                      << " ddrB=$"
                      << std::setw(2)
                      << static_cast<int>(registers.ddrB)
                      << std::dec << "\n";
        #endif

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

            if (!timerARunning)
                timerACounter = timerALatch;

            interruptStatus &= static_cast<uint8_t>(~INTERRUPT_TIMER_A);

            if (auto* drive = dynamic_cast<Drive*>(parentPeripheral))
                drive->updateIRQ();

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

            interruptStatus &= static_cast<uint8_t>(~INTERRUPT_TIMER_B);

            if (auto* drive = dynamic_cast<Drive*>(parentPeripheral))
                drive->updateIRQ();

            break;
        }
        case 0x08: registers.tod10th = value; break;
        case 0x09: registers.todSeconds = value; break;
        case 0x0A: registers.todMinutes = value; break;
        case 0x0B: registers.todHours = value; break;
        case 0x0C:
        {
            registers.serialData = value;

            // Prepare CIA serial output/input shift state.
            // In output mode, Timer A underflows clock out the SDR and
            // the CIA raises serial interrupt after 8 bits.
            serialShiftRegister = value;
            serialBitCount = 0;
            serialRxJustArmed = true;
            lastCntLevel = cntLevel;
            lastSpLevel = spLevel;

            break;
        }
        case 0x0D:
        {
            uint8_t mask = value & 0x1F;

            if (value & 0x80)
                registers.interruptEnable |= mask;
            else
                registers.interruptEnable &= static_cast<uint8_t>(~mask);

            if ((interruptStatus & registers.interruptEnable & 0x1F) != 0)
                interruptStatus |= 0x80;
            else
                interruptStatus &= 0x7F;

            if (auto* drive = dynamic_cast<Drive*>(parentPeripheral))
                drive->updateIRQ();

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

            // If serial mode changed, discard any partial byte receive.
            if ((old ^ value) & CRA_SPMODE)
            {
                serialShiftRegister = 0x00;
                serialBitCount = 0;
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
    const uint8_t mask = static_cast<uint8_t>(bit) & 0x1F;

    interruptStatus = static_cast<uint8_t>((interruptStatus & 0x1F) | mask);

    if ((interruptStatus & registers.interruptEnable & 0x1F) != 0)
        interruptStatus |= 0x80;
    else
        interruptStatus &= 0x7F;

    if (auto* drive = dynamic_cast<Drive*>(parentPeripheral))
        drive->updateIRQ();
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

    cntLevel = (portBPins & PRB_CLKIN)  != 0;
    spLevel  = (portBPins & PRB_DATAIN) != 0;
}

void DriveCIA::applyIECOutputs()
{
    auto* drive = dynamic_cast<Drive*>(parentPeripheral);
    if (!drive) return;

    const uint8_t ddrB  = registers.ddrB;
    const uint8_t portB = registers.portB;

    // Bus output gate: only active if the ROM configured the control bit
    // as output and drove it active.
    const bool busDriversEnabled =
        ((ddrB & PRB_BUSDIR) != 0) &&
        ((portB & PRB_BUSDIR) != 0);

    const bool datOutAssertLow =
        busDriversEnabled &&
        ((ddrB & PRB_DATOUT) != 0) &&
        ((portB & PRB_DATOUT) != 0);

    const bool clkOutAssertLow =
        busDriversEnabled &&
        ((ddrB & PRB_CLKOUT) != 0) &&
        ((portB & PRB_CLKOUT) != 0);

    // Hardware ATN acknowledge helper:
    // if ATN is physically low and the ROM enables PB4 as output/high,
    // the drive pulls DATA low.
    const bool hwAtnAckDataLow =
        iecAtnInLow &&
        ((ddrB & PRB_ATNACK) != 0) &&
        ((portB & PRB_ATNACK) != 0);

    const bool driveDataLow = datOutAssertLow || hwAtnAckDataLow;
    const bool driveClkLow  = clkOutAssertLow;

    drive->peripheralAssertData(driveDataLow);
    drive->peripheralAssertClk(driveClkLow);

#ifdef Debug
    std::cout << "[CIA] applyIECOutputs:"
              << " atnLow=" << (iecAtnInLow ? 1 : 0)
              << " busDriversEnabled=" << (busDriversEnabled ? 1 : 0)
              << " hwAtnAckDataLow=" << (hwAtnAckDataLow ? 1 : 0)
              << " datOutLow=" << (datOutAssertLow ? 1 : 0)
              << " clkOutLow=" << (clkOutAssertLow ? 1 : 0)
              << " -> driveDataLow=" << (driveDataLow ? 1 : 0)
              << " driveClkLow=" << (driveClkLow ? 1 : 0)
              << " ddrB=$" << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
              << static_cast<int>(ddrB)
              << " portB=$" << std::setw(2)
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

    // IEC inputs are inverted before reaching CIA-visible side:
    // physical LOW => CIA pin HIGH.
    cntLevel = iecClkInLow;
    spLevel  = iecDataInLow;

    applyIECInputsToPortBPins();

    if (atnChanged)
        notifyAtnInput(atnLow);

    applyIECOutputs();
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
    std::cout << "[CIA] primeAtnLevel called atnLow=" << atnLow << "\n";
#endif

    // Sync edge detector baseline only.
    lastAtnLow = atnLow;
}
