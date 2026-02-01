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
    interruptStatus(0x00)
{

}

DriveCIA::~DriveCIA() = default;

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
}

void DriveCIA::tick(uint32_t cycles)
{
    while(cycles-- > 0)
    {
        // Update Pin B
        updatePinsFromBus();

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

    if (wiring && wiring->samplePortAPins) wiring->samplePortAPins(*this, *drive, portAPins);
    if (wiring && wiring->samplePortBPins) wiring->samplePortBPins(*this, *drive, portBPins);
}

void DriveCIA::applyIECOutputs()
{
    if (!parentPeripheral) return;

    // Output bits only matter when DDR says output
    const bool datOutLow = (registers.ddrB & PRB_DATOUT) && ((registers.portB & PRB_DATOUT) == 0);
    const bool clkOutLow = (registers.ddrB & PRB_CLKOUT) && ((registers.portB & PRB_CLKOUT) == 0);
    const bool atnAckLow = (registers.ddrB & PRB_ATNACK) && ((registers.portB & PRB_ATNACK) == 0);

    bool busdirEnable = true;
    if (registers.ddrB & PRB_BUSDIR)
        busdirEnable = ((registers.portB & PRB_BUSDIR) != 0);

    const bool driveDataLow = busdirEnable && (datOutLow || atnAckLow);
    const bool driveClkLow  = busdirEnable && clkOutLow;

    parentPeripheral->peripheralAssertData(driveDataLow);
    parentPeripheral->peripheralAssertClk(driveClkLow);
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
