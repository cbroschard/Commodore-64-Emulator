// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Drive/D1541.h"
#include "Drive/D1541VIA.h"
#include "IECBUS.h"
#include "Peripheral.h"

D1541VIA::D1541VIA() :
    parentPeripheral(nullptr),
    viaRole(VIARole::Unknown),
    srShiftReg(0),
    srBitCount(0),
    srShiftInMode(false),
    ledOn(false),
    mechDataLatch(0xFF),
    mechBytePending(false),
    t1Counter(0),
    t1Latch(0),
    t1Running(false),
    t2Counter(0),
    t2Latch(0),
    t2Running(false),
    srCount(0)
{
    reset();
}

D1541VIA::~D1541VIA() = default;

void D1541VIA::attachPeripheralInstance(Peripheral* parentPeripheral, VIARole viaRole)
{
    this->parentPeripheral = parentPeripheral;
    this->viaRole = viaRole;
}

void D1541VIA::reset()
{
    portAPins                           = 0xFF;
    portBPins                           = 0xFF;

    // Initialize registers
    registers.orbIRB                    = 0xFF;
    registers.oraIRA                    = 0x00;
    registers.ddrB                      = 0x00;
    registers.ddrA                      = 0x00;
    registers.timer1CounterLowByte      = 0x00;
    registers.timer1CounterHighByte     = 0x00;
    registers.timer1LowLatch            = 0x00;
    registers.timer1HighLatch           = 0x00;
    registers.timer2CounterLowByte      = 0x00;
    registers.timer2CounterHighByte     = 0x00;
    registers.serialShift               = 0x00;
    registers.auxControlRegister        = 0x00;
    registers.peripheralControlRegister = 0x00;
    registers.interruptFlag             = 0x00;
    registers.interruptEnable           = 0x00;
    registers.oraIRANoHandshake         = 0x00;

    // Timers
    t1Counter                           = 0x00;
    t1Latch                             = 0x00;
    t1Running                           = false;
    t2Counter                           = 0x00;
    t2Latch                             = 0x00;
    t2Running                           = false;

    // Drive Mechanics
    ledOn                               = false;
    syncDetected                        = false;
    mechBytePending                     = false;
    mechDataLatch                       = 0xFF;

    // Serial shift
    srShiftReg                          = 0;
    srBitCount                          = 0;
    srCount                             = 0;
    srShiftInMode                       = false;
}

void D1541VIA::resetShift()
{
    srShiftReg = 0;
    srBitCount = 0;
    srShiftInMode = false;
}

void D1541VIA::tick(uint32_t cycles)
{
    while(cycles-- > 0)
    {
        // Timer 1
        if (t1Running)
        {
            if (t1Counter > 0)
            {
                --t1Counter;

                // Reflect back into the visible counter registers
                registers.timer1CounterLowByte  = static_cast<uint8_t>(t1Counter & 0x00FF);
                registers.timer1CounterHighByte = static_cast<uint8_t>((t1Counter >> 8) & 0x00FF);

                if (t1Counter == 0)
                {
                    // Set IFR6
                     triggerInterrupt(IFR_TIMER1);

                    // Check ACR bit 6 to decide one-shot vs continuous
                    bool t1Continuous = (registers.auxControlRegister & 0x40) != 0;

                    if (t1Continuous)
                    {
                        // Free-run: reload from latch and keep going
                        t1Counter = t1Latch;
                    }
                    else
                    {
                        // One-shot: stop the timer
                        t1Running = false;
                    }
                }
            }
        }

        // Timer 2
        if (t2Running)
        {
            if (t2Counter > 0)
            {
                --t2Counter;

                registers.timer2CounterLowByte  = static_cast<uint8_t>(t2Counter & 0x00FF);
                registers.timer2CounterHighByte = static_cast<uint8_t>((t2Counter >> 8) & 0x00FF);

                if (t2Counter == 0)
                {
                    // Set IFR bit 5
                     triggerInterrupt(IFR_TIMER2);

                    // Free-running: reload from latch and keep going
                    t2Counter = t2Latch;
                }
            }
        }
    }
}

uint8_t D1541VIA::readRegister(uint16_t address)
{
    switch(address)
    {
        case 0x00:
        {
            const uint8_t ddrB  = registers.ddrB;
            uint8_t value = static_cast<uint8_t>((registers.orbIRB & ddrB) | (portBPins & static_cast<uint8_t>(~ddrB)));

            if (viaRole == VIARole::VIA1_IECBus)
            {
                if (auto* drive = dynamic_cast<D1541*>(parentPeripheral))
                {
                    // Device number DIP "switches" live on PB5/PB6 as inputs (only override if those bits are inputs)
                    int dev = drive->getDeviceNumber();
                    int offset = dev - 8;
                    if (offset < 0 || offset > 3) offset = 0;

                    if ((ddrB & (1u << IEC_DEV_BIT0)) == 0)
                    {
                        if (offset & 0x01) value |=  (1u << IEC_DEV_BIT0);
                        else               value &= static_cast<uint8_t>(~(1u << IEC_DEV_BIT0));
                    }

                    if ((ddrB & (1u << IEC_DEV_BIT1)) == 0)
                    {
                        if (offset & 0x02) value |=  (1u << IEC_DEV_BIT1);
                        else               value &= static_cast<uint8_t>(~(1u << IEC_DEV_BIT1));
                    }
                }
            }
            else if (viaRole == VIARole::VIA2_Mechanics)
            {
                if (auto* drive = dynamic_cast<D1541*>(parentPeripheral))
                {
                    // WP input (active low)
                    if ((ddrB & (1u << MECH_WRITE_PROTECT)) == 0)
                    {
                        bool wpLow = drive->isWriteProtected();
                        if (wpLow) value &= static_cast<uint8_t>(~(1u << MECH_WRITE_PROTECT));
                        else       value |=  static_cast<uint8_t>( (1u << MECH_WRITE_PROTECT));
                    }

                    if ((ddrB & (1u << MECH_SYNC_DETECTED)) == 0)
                    {
                        bool sync = isSyncDetected();
                        if (sync)   value &= ~(1u << MECH_SYNC_DETECTED); // active-low: SYNC => 0
                        else        value |=  (1u << MECH_SYNC_DETECTED); // no sync => 1
                    }
                }
            }

            return value;
        }
        case 0x01: // ORA/IRA (Port A)
        {
            const uint8_t ddrA  = registers.ddrA;

            // Default: normal VIA port behavior
            uint8_t value = static_cast<uint8_t>(registers.oraIRA & ddrA);

            uint8_t inputPins = portAPins;

            if (viaRole == VIARole::VIA1_IECBus)
            {
                if (auto* drive = dynamic_cast<D1541*>(parentPeripheral))
                {
                    // The 1541 ROM steps the head back until this bit goes LOW.
                    if ((ddrA & (1u << PORTA_TRACK0_SENSOR)) == 0)
                    {
                        bool atTrack0 = drive->isTrack0();
                        if (atTrack0) inputPins &= static_cast<uint8_t>(~(1u << PORTA_TRACK0_SENSOR));
                        else          inputPins |= (1u << PORTA_TRACK0_SENSOR);
                    }

                    // The 1541 ROM polls this bit to know when to read VIA2.
                    if ((ddrA & (1u << PORTA_BYTE_READY)) == 0)
                    {
                        bool byteReadyLow = drive->getByteReadyLow(); // You added this to D1541.h
                        if (byteReadyLow) inputPins &= static_cast<uint8_t>(~(1u << PORTA_BYTE_READY));
                        else              inputPins |= (1u << PORTA_BYTE_READY);
                    }
                }
            }

            // Combine inputs with the DDR mask
            value |= (inputPins & static_cast<uint8_t>(~ddrA));

            if (viaRole == VIARole::VIA2_Mechanics)
            {
                value = static_cast<uint8_t>((registers.oraIRA & ddrA) | (mechDataLatch & static_cast<uint8_t>(~ddrA)));

                // Reading PRA consumes the pending byte and clears CA1 IFR
                if (mechBytePending)
                {
                    mechBytePending = false;
                    clearIFR(IFR_CA1);
                }
            }
            return value;
        }
        case 0x02: return registers.ddrB;
        case 0x03: return registers.ddrA;
        case 0x04: return registers.timer1CounterLowByte;
        case 0x05:
        {
            uint8_t value = registers.timer1CounterHighByte;
            clearIFR(IFR_TIMER1);
            return value;
        }
        case 0x06: return registers.timer1LowLatch;
        case 0x07: return registers.timer1HighLatch;
        case 0x08: return registers.timer2CounterLowByte;
        case 0x09: return registers.timer2CounterHighByte;
        case 0x0A:
        {
            clearIFR(IFR_SR);
            if (viaRole == VIARole::VIA2_Mechanics)
            {
                return mechDataLatch;
            }
            return registers.serialShift;
        }
        case 0x0B: return registers.auxControlRegister;
        case 0x0C: return registers.peripheralControlRegister;
        case 0x0D:
        {
            refreshMasterBit();
            return registers.interruptFlag;
        }
        case 0x0E: return registers.interruptEnable;
        case 0x0F: return registers.oraIRANoHandshake;
        default: return 0xFF;
    }
}

void D1541VIA::writeRegister(uint16_t address, uint8_t value)
{
    switch(address)
    {
        case 0x00:
        {
            uint8_t prevORB  = registers.orbIRB;
            registers.orbIRB = value;

            if (viaRole == VIARole::VIA1_IECBus)
            {
                // Recompute IEC outputs based on (ORB, DDRB)
                updateIECOutputsFromPortB();
            }
            if (viaRole == VIARole::VIA2_Mechanics)
            {
                if (auto* drive = dynamic_cast<D1541*>(parentPeripheral))
                {
                    uint8_t ddrB = registers.ddrB;

                    // stepper phase (1541-style head move)
                    const uint8_t phaseMask = 0x03;

                    const uint8_t oldPhase = prevORB & phaseMask;
                    const uint8_t newPhase = registers.orbIRB & phaseMask;

                    if (oldPhase != newPhase)
                    {
                        #ifdef Debug
                        std::cout << "[VIA2] step phase " << int(oldPhase) << " -> " << int(newPhase) << "\n";
                        #endif
                        drive->onStepperPhaseChange(oldPhase, newPhase);
                    }
                    // Bit 2: Motor Control
                    if (ddrB & (1u << MECH_SPINDLE_MOTOR))
                    {
                        bool enable = (registers.orbIRB & (1u << MECH_SPINDLE_MOTOR)) != 0;
                        if (enable) drive->startMotor();
                        else        drive->stopMotor();
                    }

                    // Bit 3: LED
                    if (ddrB & (1u << MECH_LED))
                    {
                        bool on = (registers.orbIRB & (1u << MECH_LED)) != 0;
                        setLed(on);
                    }

                    // Bit 5 & 6: Density Code
                    if (ddrB & ((1u << MECH_DENSITY_BIT0) | (1u << MECH_DENSITY_BIT1)))
                    {
                        uint8_t orb  = registers.orbIRB;
                        uint8_t code = ((orb >> MECH_DENSITY_BIT0) & 0x01) | (((orb >> MECH_DENSITY_BIT1) & 0x01) << 1);
                        drive->setDensityCode(code);
                    }
                }
            }
            break;
        }
        case 0x01:
        {
            uint8_t ddrA = registers.ddrA;
            registers.oraIRA = value;

            if (viaRole == VIARole::VIA1_IECBus)
            {
                if (auto* drive = dynamic_cast<D1541*>(parentPeripheral))
                {
                    // Bit 1 Bus Driver Selection
                    if (ddrA & (1u << PORTA_FSM_DIRECTION))
                    {
                        bool output = (value & (1u << PORTA_FSM_DIRECTION)) != 0;
                        drive->setBusDriversEnabled(output);
                        updateIECOutputsFromPortB();
                    }
                }
            }
            break;
        }
        case 0x02:
        {
            registers.ddrB = value;

            if (viaRole == VIARole::VIA1_IECBus)
            {
                updateIECOutputsFromPortB(); // direction changes can assert/release lines
            }
            else if (viaRole == VIARole::VIA2_Mechanics)
            {
                if (auto* drive = dynamic_cast<D1541*>(parentPeripheral))
                {
                    const uint8_t orb  = registers.orbIRB;
                    const uint8_t ddrB = registers.ddrB;

                    // Motor
                    if (ddrB & (1u << MECH_SPINDLE_MOTOR))
                    {
                        bool enable = (orb & (1u << MECH_SPINDLE_MOTOR)) != 0;
                        enable ? drive->startMotor() : drive->stopMotor();
                    }

                    // LED
                    if (ddrB & (1u << MECH_LED))
                    {
                        setLed((orb & (1u << MECH_LED)) != 0);
                    }

                    // Density (require both bits as outputs to be “valid”)
                    const uint8_t densMask = (1u << MECH_DENSITY_BIT0) | (1u << MECH_DENSITY_BIT1);
                    if ((ddrB & densMask) == densMask)
                    {
                        uint8_t code =
                            ((orb >> MECH_DENSITY_BIT0) & 0x01) |
                            (((orb >> MECH_DENSITY_BIT1) & 0x01) << 1);
                        drive->setDensityCode(code);
                    }
                }
            }
            break;
        }
        case 0x03:
        {
            registers.ddrA = value;

            if (viaRole == VIARole::VIA1_IECBus)
            {
                if (auto* drive = dynamic_cast<D1541*>(parentPeripheral))
                {
                    const uint8_t ora  = registers.oraIRA;
                    const uint8_t ddrA = registers.ddrA;

                    // If you keep these hooks for shared code: bus driver enable is OK;
                    // head-side + burst should be NO-OPs on D1541.
                    if (ddrA & (1u << PORTA_FSM_DIRECTION))
                    {
                        bool output = (ora & (1u << PORTA_FSM_DIRECTION)) != 0;
                        drive->setBusDriversEnabled(output);
                        updateIECOutputsFromPortB();
                    }
                }
            }
            break;
        }
        case 0x04: // T1C-L
        {
            registers.timer1CounterLowByte = value;
            t1Counter = (t1Counter & 0xFF00) | value;
            break;
        }
        case 0x05: // T1C-H (loads/starts)
        {
            registers.timer1CounterHighByte = value;
            t1Counter = (static_cast<uint16_t>(registers.timer1CounterHighByte) << 8) |
                        static_cast<uint16_t>(registers.timer1CounterLowByte);

            t1Running = true;
            clearIFR(IFR_TIMER1);
            break;
        }
        case 0x06: // T1 Latch L
        {
            registers.timer1LowLatch = value;
            t1Latch = (t1Latch & 0xFF00) | value;
            break;
        }
        case 0x07: // T1 Latch H
        {
            registers.timer1HighLatch = value;
            t1Latch = (static_cast<uint16_t>(registers.timer1HighLatch) << 8) |
                      static_cast<uint16_t>(registers.timer1LowLatch);
            break;
        }
        case 0x08: // T2C-L
        {
            registers.timer2CounterLowByte = value;
            t2Counter = (t2Counter & 0xFF00) | value;
            break;
        }
        case 0x09: // T2C-H (loads/starts)
        {
            registers.timer2CounterHighByte = value;
            t2Counter = (static_cast<uint16_t>(registers.timer2CounterHighByte) << 8) |
                        static_cast<uint16_t>(registers.timer2CounterLowByte);

            t2Latch   = t2Counter;
            t2Running = true;
            clearIFR(IFR_TIMER2);
            break;
        }
        case 0x0A:
        {
            registers.serialShift = value;
            break;
        }
        case 0x0B:
        {
            registers.auxControlRegister = value;
            break;
        }
        case 0x0C:
        {
            registers.peripheralControlRegister = value;
            break;
        }
        case 0x0D:
        {
            uint8_t mask = value & 0x7F;
            // Clear any bits where a 1 was written
            clearIFR(mask);
            break;
        }
        case 0x0E:
        {
            bool set = (value & 0x80) != 0;
            uint8_t mask = value & 0x7F;
            if (set)
            {
                registers.interruptEnable |= mask;
            }
            else
            {
                registers.interruptEnable &= ~mask;
            }
            refreshMasterBit();
            break;
        }
        case 0x0F:
        {
            // Same as 0x01 but without driving ATN/SRQ
            registers.oraIRANoHandshake = (registers.oraIRANoHandshake & ~registers.ddrA) | (value & registers.ddrA);
            break;
        }
        default: break;
    }
}

void D1541VIA::diskByteFromMedia(uint8_t byte, bool inSync)
{
    if (viaRole != VIARole::VIA2_Mechanics) return;

    // Update sync marker state (PB7 handling happens in readRegister($00))
    setSyncDetected(inSync);

    // During SYNC marks, don't deliver bytes / don't generate byte-ready events
    if (inSync)
    {
        mechBytePending = false;
        clearIFR(IFR_CA1);
        return;
    }

    if (mechBytePending)
        return;

    mechDataLatch   = byte;
    mechBytePending = true;

    triggerInterrupt(IFR_CA1);

    // Pulse SO to set V flag for the BVC loop
    if (parentPeripheral)
    {
        auto* drive = static_cast<D1541*>(parentPeripheral);
        drive->asDrive()->getDriveCPU()->pulseSO();
    }
}

void D1541VIA::setIECInputLines(bool atnLow, bool clkLow, bool dataLow)
{
    // remember previous ATN state as seen on PB7
    bool prevAtnLow = ((portBPins & (1u << IEC_ATN_IN_BIT)) != 0);

    uint8_t pins = portBPins;

    if (dataLow) pins |= (uint8_t) (1u << IEC_DATA_IN_BIT); // Bus Low -> Pin High
    else         pins &= (uint8_t)~(1u << IEC_DATA_IN_BIT); // Bus High -> Pin Low

    if (clkLow)  pins |= (uint8_t) (1u << IEC_CLK_IN_BIT);  // Bus Low -> Pin High
    else         pins &= (uint8_t)~(1u << IEC_CLK_IN_BIT);  // Bus High -> Pin Low

    if (atnLow)  pins |= (uint8_t) (1u << IEC_ATN_IN_BIT);
    else         pins &= (uint8_t)~(1u << IEC_ATN_IN_BIT);

    portBPins = pins;

    bool newAtnLow = ((portBPins & (1u << IEC_ATN_IN_BIT)) != 0); // Active is High at Pin

    if (viaRole == VIARole::VIA1_IECBus && (newAtnLow != prevAtnLow))
        updateIECOutputsFromPortB();
}

void D1541VIA::clearMechBytePending()
{
    mechBytePending = false;
    clearIFR(IFR_CA1);
}

bool D1541VIA::checkIRQActive() const
{
    uint8_t active = registers.interruptEnable & registers.interruptFlag & 0x7F;
    return active != 0;
}

void D1541VIA::triggerInterrupt(uint8_t mask)
{
    registers.interruptFlag |= mask;
    refreshMasterBit();
}

void D1541VIA::clearIFR(uint8_t mask)
{
    registers.interruptFlag &= static_cast<uint8_t>(~mask);
    refreshMasterBit();
}

void D1541VIA::refreshMasterBit()
{
    const uint8_t pendingEnabled = registers.interruptFlag & registers.interruptEnable & 0x7F;

    if (pendingEnabled)
        registers.interruptFlag |= IFR_IRQ;
    else
        registers.interruptFlag &= static_cast<uint8_t>(~IFR_IRQ);
}

void D1541VIA::onClkEdge(bool rising, bool falling)
{
    if (viaRole != VIARole::VIA1_IECBus)
        return;

    // 6522: ACR bits 2..3 = 01 => shift-in under external clock
    srShiftInMode = (registers.auxControlRegister & 0x0C) == 0x04;

    if (rising && srShiftInMode)
    {
        bool dataLow = false;
        if (auto* drive = dynamic_cast<D1541*>(parentPeripheral))
        {
            dataLow = drive->getDataLineLow();
        }

        const int bit = dataLow ? 0 : 1;

        srShiftReg |= static_cast<uint8_t>(bit << srBitCount);
        ++srBitCount;

        if (srBitCount == 8)
        {
            registers.serialShift = srShiftReg;

            #ifdef Debug
            std::cout << "[VIA1] IEC RX byte = $"
                      << std::hex << std::uppercase << int(registers.serialShift)
                      << " (LSB-first, ACR=$" << int(registers.auxControlRegister)
                      << ")\n" << std::dec;
            #endif

            // reset for next byte
            srShiftReg = 0;
            srBitCount = 0;

            // raise SR interrupt
            triggerInterrupt(IFR_SR);
        }
    }
}

void D1541VIA::onCA1Edge(bool rising, bool falling)
{
    // PCR Bit 0 controls CA1 Active Edge
    // 0 = Negative Edge (High to Low)
    // 1 = Positive Edge (Low to High)
    bool activeEdgePos = (registers.peripheralControlRegister & 0x01) != 0;
    bool trigger = false;

    if (activeEdgePos && rising) trigger = true;
    else if (!activeEdgePos && falling) trigger = true;

    // Interrupt handling for CA1
    if (trigger)
    {
        triggerInterrupt(IFR_CA1);
    }

}

void D1541VIA::updateIECOutputsFromPortB()
{
    if (viaRole != VIARole::VIA1_IECBus) return;

    auto* d1541 = dynamic_cast<D1541*>(parentPeripheral);
    if (!d1541) return;

    if (!d1541->isBusDriversEnabled())
    {
        d1541->driveControlDataLine(false); // release
        d1541->driveControlClkLine(false);  // release
        return;
    }

    const uint8_t orb  = registers.orbIRB;
    const uint8_t ddrB = registers.ddrB;

    bool dataLow = false;
    bool clkLow  = false;

    // Output logic: Inverted Open Collector buffers (7406).
    // VIA Output '1' -> Buffer Input '1' -> Buffer Output '0' (Low/Active).
    // VIA Output '0' -> Buffer Input '0' -> Buffer Output High-Z (Released).

    if (ddrB & (1u << IEC_DATA_OUT_BIT))
        dataLow = ((orb & (1u << IEC_DATA_OUT_BIT)) != 0);

    if (ddrB & (1u << IEC_CLK_OUT_BIT))
        clkLow = ((orb & (1u << IEC_CLK_OUT_BIT)) != 0);

    // ATN Input bit (Bit 7, PB7 or similar)
    bool atnAsserted = ((portBPins & (1u << IEC_ATN_IN_BIT)) != 0);

    bool atnAckAuto = false;
    if (ddrB & (1u << IEC_ATN_ACK_BIT))
        atnAckAuto = ((orb & (1u << IEC_ATN_ACK_BIT)) == 0);

    if (atnAsserted && atnAckAuto)
        dataLow = true; // force DATA low as the acknowledge

    d1541->driveControlDataLine(dataLow);
    d1541->driveControlClkLine(clkLow);
}

DriveVIABase::MechanicsInfo D1541VIA::getMechanicsInfo() const
{
    MechanicsInfo m{};
    m.valid = false;

    // Only VIA2 in mechanics role has meaningful data
    if (viaRole != VIARole::VIA2_Mechanics)
        return m;

    uint8_t orb  = registers.orbIRB;
    uint8_t ddrB = registers.ddrB;

    m.valid = true;

    if (ddrB & (1u << MECH_SPINDLE_MOTOR))
    {
        m.motorOn = (orb & (1u << MECH_SPINDLE_MOTOR)) != 0;
    }
    else
    {
        m.motorOn = false; // or leave as-is
    }

    if (ddrB & (1u << MECH_LED))
    {
        m.ledOn = (orb & (1u << MECH_LED)) != 0;
    }
    else
    {
        m.ledOn = false;
    }

    // Density bits: PB5/PB6
    uint8_t code = 0;
    if (ddrB & (1u << MECH_DENSITY_BIT0))
        code |= (orb >> MECH_DENSITY_BIT0) & 0x01;
    if (ddrB & (1u << MECH_DENSITY_BIT1))
        code |= ((orb >> MECH_DENSITY_BIT1) & 0x01) << 1;

    m.densityCode = code;

    return m;
}
