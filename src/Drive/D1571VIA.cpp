// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Drive/D1571.h"
#include "Drive/D1571VIA.h"

D1571VIA::D1571VIA() :
    parentPeripheral(nullptr),
    srShiftReg(0),
    srBitCount(0),
    srShiftInMode(false),
    viaRole(VIARole::Unknown),
    ledOn(false),
    syncDetectedLow(false),
    mechDataLatch(0xFF),
    mechBytePending(false),
    t1Counter(0),
    t1Latch(0),
    t1Running(false),
    t2Counter(0),
    t2Latch(0),
    t2Running(false)
{
    reset();
}

D1571VIA::~D1571VIA() = default;

void D1571VIA::attachPeripheralInstance(Peripheral* parentPeripheral, VIARole viaRole)
{
    this->parentPeripheral = parentPeripheral;
    this->viaRole = viaRole;
    if (viaRole == VIARole::VIA1_IECBus) updateIECOutputsFromPortB();
}

void D1571VIA::reset()
{
    portAPins = 0xFF;
    portBPins = 0xFF;

    // Initialize registers
    registers.orbIRB = 0xFF;
    registers.oraIRA = 0x00;
    registers.ddrB = 0x00;
    registers.ddrA = 0x00;
    registers.timer1CounterLowByte = 0x00;
    registers.timer1CounterHighByte = 0x00;
    registers.timer1LowLatch = 0x00;
    registers.timer1HighLatch = 0x00;
    registers.timer2CounterLowByte = 0x00;
    registers.timer2CounterHighByte = 0x00;
    registers.serialShift = 0x00;
    registers.auxControlRegister = 0x00;
    registers.peripheralControlRegister = 0x00;
    registers.interruptFlag = 0x00;
    registers.interruptEnable = 0x00;
    registers.oraIRANoHandshake = 0x00;

    t1Counter = 0;
    t1Latch   = 0;
    t1Running = false;

    t2Counter = 0;
    t2Latch   = 0;
    t2Running = false;

    // Mechanics
    ledOn           = false;
    syncDetectedLow = false;
    mechDataLatch   = 0xFF;
    mechBytePending = false;

    // Serial shift
    srShiftReg    = 0;
    srBitCount    = 0;
    srShiftInMode = false;

    if (viaRole == VIARole::VIA1_IECBus)
    {
        portBPins &= static_cast<uint8_t>(~(
            (1u << IEC_ATN_IN_BIT) |
            (1u << IEC_CLK_IN_BIT) |
            (1u << IEC_DATA_IN_BIT)
        ));
    }

    if (viaRole == VIARole::VIA1_IECBus)
        updateIECOutputsFromPortB(); // forces bus release based on DDRB/ORB
}

void D1571VIA::tick()
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

uint8_t D1571VIA::readRegister(uint16_t address)
{
    switch(address)
    {
        case 0x00:
        {
            const uint8_t ddrB  = registers.ddrB;
            uint8_t value = static_cast<uint8_t>((registers.orbIRB & ddrB) | (portBPins & static_cast<uint8_t>(~ddrB)));

            if (viaRole == VIARole::VIA1_IECBus)
            {
                if (auto* drive = dynamic_cast<D1571*>(parentPeripheral))
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
                if (auto* drive = dynamic_cast<D1571*>(parentPeripheral))
                {
                    // WP input (active low)
                    if ((ddrB & (1u << MECH_WRITE_PROTECT)) == 0)
                    {
                        bool wpLow = drive->fdcIsWriteProtected();
                        if (wpLow) value &= static_cast<uint8_t>(~(1u << MECH_WRITE_PROTECT));
                        else       value |=  static_cast<uint8_t>( (1u << MECH_WRITE_PROTECT));
                    }

                    // Sync detected input (active low)
                    if ((ddrB & (1u << MECH_SYNC_DETECTED)) == 0)
                    {
                        bool syncLow = isSyncDetectedLow();
                        if (syncLow) value &= static_cast<uint8_t>(~(1u << MECH_SYNC_DETECTED));
                        else         value |=  static_cast<uint8_t>( (1u << MECH_SYNC_DETECTED));
                    }
                }
            }

            return value;
        }
        case 0x01:
        {
            uint8_t ddrA  = registers.ddrA;
            uint8_t value = static_cast<uint8_t>((registers.oraIRA & ddrA) | (portAPins & static_cast<uint8_t>(~ddrA)));
            if (viaRole == VIARole::VIA1_IECBus)
            {
                if (auto* drive = dynamic_cast<D1571*>(parentPeripheral))
                {
                    // Bit 0 Track 0
                    if ((ddrA & (1u << PORTA_TRACK0_SENSOR)) == 0)
                    {
                        bool atTrack0 = drive->isTrack0();
                        if (atTrack0) value &= (~(1u << PORTA_TRACK0_SENSOR));
                        else          value |= (1u << PORTA_TRACK0_SENSOR);
                    }

                    // Bits 3&4 unused/pulled high
                    if ((ddrA & (1u << PORTA_UNUSED3)) == 0)
                    {
                        value |= (1u << PORTA_UNUSED3);
                    }
                    if ((ddrA & (1u << PORTA_UNUSED4)) == 0)
                    {
                        value |= (1u << PORTA_UNUSED4);
                    }

                    // Bit 7 BYTE READY
                    if ((ddrA & (1u << PORTA_BYTE_READY)) == 0)
                    {
                        bool byteReadLow = drive->getByteReadyLow();
                        if (byteReadLow) value &= (~(1u << PORTA_BYTE_READY));
                        else             value |= (1u << PORTA_BYTE_READY);
                    }
                }
            }
            else if (viaRole == VIARole::VIA2_Mechanics)
            {
                // Present disk byte on input pins (bits where DDR=0)
                value = static_cast<uint8_t>((registers.oraIRA & ddrA) |
                                             (mechDataLatch & static_cast<uint8_t>(~ddrA)));

                // Reading Port A consumes "byte pending" and clears CA1 IFR
                if (mechBytePending)
                {
                    #ifdef Debug
                    std::cout << "[VIA2] PRA consume $" << std::hex << int(mechDataLatch) << std::dec << "\n";
                    #endif
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
                uint8_t ret = mechDataLatch;
                if (mechBytePending)
                {
                    mechBytePending = false;
                    clearIFR(IFR_CA1);
                }
                return ret;
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
        default: return 0xFF; // open bus
    }
    return 0xFF;
}

void D1571VIA::writeRegister(uint16_t address, uint8_t value)
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
                if (auto* drive = dynamic_cast<D1571*>(parentPeripheral))
                {
                    uint8_t ddrB = registers.ddrB;

                    // PB0/PB1: stepper phase (1541-style head move)
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
                    if (auto* drive = dynamic_cast<D1571*>(parentPeripheral))
                    {
                        // Bit 1 Bus Driver Selection
                        if (ddrA & (1u << PORTA_FSM_DIRECTION))
                        {
                            bool output = (value & (1u << PORTA_FSM_DIRECTION)) != 0;
                            drive->setBusDriversEnabled(output);
                            updateIECOutputsFromPortB();
                        }

                        // Bit 2 Head Side Select
                        if (ddrA & (1u << PORTA_RWSIDE_SELECT))
                        {
                            bool side1 = (value & (1u << PORTA_RWSIDE_SELECT)) != 0;
                            drive->setHeadSide(side1); // True = side 1(top)
                        }

                        // Bit 5 PHI2 Clock Select
                        if (ddrA & (1u << PORTA_PHI2_CLKSEL))
                        {
                            bool twoMHz = (value & (1u << PORTA_PHI2_CLKSEL)) != 0;
                            drive->setBurstClock2MHz(twoMHz);
                        }
                    }
                }
                break;
            }
        case 0x02:
        {
            registers.ddrB = value;
            uint8_t orb = registers.orbIRB;

            if (viaRole == VIARole::VIA1_IECBus)
            {
                // Direction changes can release or assert lines,
                // so recompute outputs again.
                updateIECOutputsFromPortB();
            }
            if (viaRole == VIARole::VIA2_Mechanics)
            {
                if (auto* drive = dynamic_cast<D1571*>(parentPeripheral))
                {
                    // Bit 2: Motor Control
                    if (value & (1u << MECH_SPINDLE_MOTOR))
                    {
                        bool enable = (orb & (1u << MECH_SPINDLE_MOTOR)) != 0;
                        if (enable) drive->startMotor();
                        else        drive->stopMotor();
                    }

                    // Bit 3: LED
                    if (value & (1u << MECH_LED))
                    {
                        bool on = (orb & (1u << MECH_LED)) != 0;
                        setLed(on);
                    }

                    // Bit 5 & 6: Density Code
                    if (value & ((1u << MECH_DENSITY_BIT0) | (1u << MECH_DENSITY_BIT1)))
                    {
                        uint8_t code = (((orb >> MECH_DENSITY_BIT0) & 0x01)) | (((orb >> MECH_DENSITY_BIT1) & 0x01) << 1);
                        drive->setDensityCode(code); // 0...3
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
                if (auto* drive = dynamic_cast<D1571*>(parentPeripheral))
                {
                    uint8_t ora = registers.oraIRA;
                    uint8_t ddrA = registers.ddrA;

                    // Bit 1 Bus Driver Selection
                    if (ddrA & (1u << PORTA_FSM_DIRECTION))
                    {
                        bool output = (ora & (1u << PORTA_FSM_DIRECTION)) != 0;
                        drive->setBusDriversEnabled(output);
                        updateIECOutputsFromPortB();
                    }

                    // Bit 2 Head Side Select
                    if (ddrA & (1u << PORTA_RWSIDE_SELECT))
                    {
                        bool side1 = (ora & (1u << PORTA_RWSIDE_SELECT));
                        drive->setHeadSide(side1);
                    }

                    // Bit 5 Phi2 Clock Select
                    if (ddrA & (1u << PORTA_PHI2_CLKSEL))
                    {
                        bool twoMHz = (ora & (1u << PORTA_PHI2_CLKSEL));
                        drive->setBurstClock2MHz(twoMHz);
                    }
                }
            }
            break;
        }
        case 0x04:
        {
            registers.timer1CounterLowByte = value;
            t1Counter = (t1Counter & 0xFF00) | value;
            break;
        }
        case 0x05:
        {
            registers.timer1CounterHighByte = value;
            t1Counter = static_cast<uint16_t>((registers.timer1CounterHighByte << 8)) | static_cast<uint16_t>(registers.timer1CounterLowByte);

            // Start timer 1
            t1Running = true;

            // Clear T1 interrupt flag (IFR bit 6) when (re)loading the counter
            clearIFR(IFR_TIMER1);
            break;
        }
        case 0x06:
        {
            registers.timer1LowLatch = value;

            // Update latch
            t1Latch = (t1Latch & 0xFF00) | value;
            break;
        }
        case 0x07:
        {
            registers.timer1HighLatch = value;

            // Update latch
            t1Latch = static_cast<uint16_t>((registers.timer1HighLatch << 8)) | static_cast<uint16_t>(registers.timer1LowLatch);
            break;
        }
        case 0x08:
        {
            registers.timer2CounterLowByte = value;
            t2Counter = (t2Counter & 0xFF00) | value;
            break;
        }
        case 0x09:
        {
            registers.timer2CounterHighByte = value;
            t2Counter = static_cast<uint16_t>((registers.timer2CounterHighByte << 8)) | static_cast<uint16_t>(registers.timer2CounterLowByte);

            // Free-running latch: keep a copy for reload
            t2Latch = t2Counter;

            // Start Timer 2 when high byte is written
            t2Running = true;

            // Clear T2 IFR bit (bit 5) on reload
            clearIFR(IFR_TIMER2);
            break;
        }
        case 0x0A: registers.serialShift = value; break;
        case 0x0B: registers.auxControlRegister = value; break;
        case 0x0C: registers.peripheralControlRegister = value; break;
        case 0x0D:
        {
            uint8_t mask = value & 0x7F;
            // Clear any bits where a 1 was written
            clearIFR(mask);
            break;
        }
        case 0x0E:
        {
            uint8_t mask = value & 0x7F;
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
        case 0x0F: registers.oraIRANoHandshake = value; break;
        default: break;
    }
}

void D1571VIA::setSyncDetected(bool low)
{
    syncDetectedLow = low;
}

void D1571VIA::diskByteFromMedia(uint8_t byte, bool syncLow)
{
    if (viaRole != VIARole::VIA2_Mechanics) return;

    mechDataLatch   = byte;
    mechBytePending = true;

    // Update SYNC input bit (on Port B in your model)
    setSyncDetected(syncLow);

    auto* drive = static_cast<D1571*>(parentPeripheral);
    if (drive) drive->asDrive()->getDriveCPU()->pulseSO();

    // Generate a CA1 "byte-ready" pulse.
    // Call both edges so PCR can choose which edge is "active".
    onCA1Edge(false, true);  // falling
    onCA1Edge(true,  false); // rising

    registers.serialShift = byte;   // mirror
    triggerInterrupt(IFR_SR);       // so SR polling/IRQ can work too
}

void D1571VIA::setIECInputLines(bool atnLow, bool clkLow, bool dataLow)
{
    // remember previous ATN state as seen on PB7
    bool prevAtnLow = ((portBPins & (1u << IEC_ATN_IN_BIT)) != 0);

    uint8_t pins = portBPins;

    // IMPORTANT: The 1571 uses inverters (e.g. 74LS14) on the inputs.
    // Bus Low (Active) -> Inverter Input Low -> Inverter Output High -> VIA Pin 1.
    // Bus High (Released) -> Inverter Input High -> Inverter Output Low -> VIA Pin 0.

    if (dataLow) pins |= (uint8_t) (1u << IEC_DATA_IN_BIT); // Bus Low -> Pin High
    else         pins &= (uint8_t)~(1u << IEC_DATA_IN_BIT); // Bus High -> Pin Low

    if (clkLow)  pins |= (uint8_t) (1u << IEC_CLK_IN_BIT);  // Bus Low -> Pin High
    else         pins &= (uint8_t)~(1u << IEC_CLK_IN_BIT);  // Bus High -> Pin Low

    if (atnLow)  pins |= (uint8_t) (1u << IEC_ATN_IN_BIT);
    else         pins &= (uint8_t)~(1u << IEC_ATN_IN_BIT);

    portBPins = pins;

    bool newAtnLow = ((portBPins & (1u << IEC_ATN_IN_BIT)) != 0); // Active is High at Pin
    // NOTE: The logic below uses newAtnLow, but updateIECOutputsFromPortB checks the pin bit.
    // This is fine as long as we are consistent that "Pin Bit 1" means "ATN Active".

    if (viaRole == VIARole::VIA1_IECBus && (newAtnLow != prevAtnLow))
        updateIECOutputsFromPortB();
}

void D1571VIA::updateIECOutputsFromPortB()
{
    if (viaRole != VIARole::VIA1_IECBus) return;

    auto* d1571 = dynamic_cast<D1571*>(parentPeripheral);
    if (!d1571) return;

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
    // NOTE: We rely on the fact that setIECInputLines sets the PIN high when Bus ATN is Low.
    bool atnAsserted = ((portBPins & (1u << IEC_ATN_IN_BIT)) != 0);

    bool atnAckAuto = false;
    if (ddrB & (1u << IEC_ATN_ACK_BIT))
        atnAckAuto = ((orb & (1u << IEC_ATN_ACK_BIT)) == 0);

    if (atnAsserted && atnAckAuto)
        dataLow = true; // force DATA low as the acknowledge

    d1571->driveControlDataLine(dataLow);
    d1571->driveControlClkLine(clkLow);
}

bool D1571VIA::checkIRQActive() const
{
    uint8_t active = registers.interruptEnable & registers.interruptFlag & 0x7F;
    return active != 0;
}

void D1571VIA::triggerInterrupt(uint8_t sourceMask)
{
    registers.interruptFlag |= sourceMask;
    refreshMasterBit();
}

void D1571VIA::clearIFR(uint8_t sourceMask)
{
    registers.interruptFlag &= static_cast<uint8_t>(~sourceMask);
    refreshMasterBit();
}

void D1571VIA::refreshMasterBit()
{
    if (registers.interruptFlag & 0x7F)
        registers.interruptFlag |= IFR_IRQ;
    else
        registers.interruptFlag &= static_cast<uint8_t>(~IFR_IRQ);
}

void D1571VIA::onClkEdge(bool rising, bool falling)
{
    if (viaRole != VIARole::VIA1_IECBus)
        return;

    // 6522: ACR bits 2..3 = 01 => shift-in under external clock
    srShiftInMode = (registers.auxControlRegister & 0x0C) == 0x04;

    if (rising && srShiftInMode)
    {
        bool dataLow = false;
        if (auto* drive = dynamic_cast<D1571*>(parentPeripheral))
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

void D1571VIA::onCA1Edge(bool rising, bool falling)
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

DriveVIABase::MechanicsInfo D1571VIA::getMechanicsInfo() const
{
    MechanicsInfo m{};
    m.valid = false;          // assume not valid unless we know we're VIA2/mech

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

    // LED: output bit, 1 = ON, 0 = OFF
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
