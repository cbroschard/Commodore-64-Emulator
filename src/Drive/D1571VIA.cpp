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
    srShiftReg(0),
    srBitCount(0),
    srShiftInMode(false),
    ledOn(false),
    syncDetected(false),
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

void D1571VIA::saveState(StateWriter& wrtr) const
{
    wrtr.writeU32(1);

    saveVIAState(wrtr);

    // Serial shift runtime
    wrtr.writeU8(srShiftReg);
    wrtr.writeU8(srBitCount);
    wrtr.writeBool(srShiftInMode);

    // Mechanical signals
    wrtr.writeBool(ledOn);
    wrtr.writeBool(syncDetected);
    wrtr.writeU8(mechDataLatch);
    wrtr.writeBool(mechBytePending);
}

bool D1571VIA::loadState(StateReader& rdr)
{
    uint32_t ver = 0;
    if (!rdr.readU32(ver)) return false;

    if (ver != 1)
        return false;

    if (!loadVIAState(rdr))
        return false;

    // Serial shift runtime
    if (!rdr.readU8(srShiftReg)) return false;
    if (!rdr.readU8(srBitCount)) return false;
    if (!rdr.readBool(srShiftInMode)) return false;

    // Mechanical signals
    if (!rdr.readBool(ledOn)) return false;
    if (!rdr.readBool(syncDetected)) return false;
    if (!rdr.readU8(mechDataLatch)) return false;
    if (!rdr.readBool(mechBytePending)) return false;

    // Post-restore fixups / derived state
    if (viaRole == DriveVIA6522::VIARole::VIA1_IECBus)
        updateIECOutputsFromPortB();

    if (viaRole == DriveVIA6522::VIARole::VIA2_Mechanics)
        recomputeDiskWriteGate();

    refreshMasterBit();

    return true;
}

void D1571VIA::reset()
{
    DriveVIA6522::reset();

    portAPins = 0x00;
    portBPins = 0x00;

    // Initialize registers
    registers.orbIRB = 0x00;
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
    ledOn            = false;
    syncDetected     = false;
    mechDataLatch    = 0xFF;
    mechBytePending  = false;

    // Serial shift
    srShiftReg    = 0;
    srBitCount    = 0;
    srShiftInMode = false;

    if (viaRole == DriveVIA6522::VIARole::VIA1_IECBus)
    {
        portBPins &= static_cast<uint8_t>(~(
            (1u << IEC_ATN_IN_BIT) |
            (1u << IEC_CLK_IN_BIT) |
            (1u << IEC_DATA_IN_BIT)
        ));
    }

    if (viaRole == DriveVIA6522::VIARole::VIA1_IECBus)
        updateIECOutputsFromPortB(); // forces bus release based on DDRB/ORB
}

uint8_t D1571VIA::readRegister(uint16_t address)
{
    address &= 0x0F;

    uint8_t timerValue = 0xFF;
    if (readTimerRegister(address, timerValue))
        return timerValue;

    uint8_t irqValue = 0xFF;
    if (readInterruptRegister(address, irqValue))
        return irqValue;

    uint8_t controlValue = 0xFF;
    if (readControlRegister(address, controlValue))
        return controlValue;

    switch (address)
    {
        case 0x00: // ORB/IRB - Port B
        {
            const uint8_t ddrB = registers.ddrB;

            uint8_t value =
                static_cast<uint8_t>(
                    (registers.orbIRB & ddrB) |
                    (portBPins & static_cast<uint8_t>(~ddrB))
                );

            if (viaRole == DriveVIA6522::VIARole::VIA1_IECBus)
            {
                if (auto* drive = dynamic_cast<D1571*>(parentPeripheral))
                {
                    // Device number DIP switches live on PB5/PB6 as inputs.
                    int dev = drive->getDeviceNumber();
                    int offset = dev - 8;

                    if (offset < 0 || offset > 3)
                        offset = 0;

                    if ((ddrB & (1u << IEC_DEV_BIT0)) == 0)
                    {
                        if (offset & 0x01)
                            value |= static_cast<uint8_t>(1u << IEC_DEV_BIT0);
                        else
                            value &= static_cast<uint8_t>(~(1u << IEC_DEV_BIT0));
                    }

                    if ((ddrB & (1u << IEC_DEV_BIT1)) == 0)
                    {
                        if (offset & 0x02)
                            value |= static_cast<uint8_t>(1u << IEC_DEV_BIT1);
                        else
                            value &= static_cast<uint8_t>(~(1u << IEC_DEV_BIT1));
                    }
                }
            }
            else if (viaRole == DriveVIA6522::VIARole::VIA2_Mechanics)
            {
                if (auto* drive = dynamic_cast<D1571*>(parentPeripheral))
                {
                    // Write protect input, active low.
                    if ((ddrB & (1u << MECH_WRITE_PROTECT)) == 0)
                    {
                        const bool wpLow = drive->fdcIsWriteProtected();

                        if (wpLow)
                            value &= static_cast<uint8_t>(~(1u << MECH_WRITE_PROTECT));
                        else
                            value |= static_cast<uint8_t>(1u << MECH_WRITE_PROTECT);
                    }

                    // Sync detect input, active low.
                    if ((ddrB & (1u << MECH_SYNC_DETECTED)) == 0)
                    {
                        const bool sync = isSyncDetected();

                        if (sync)
                            value &= static_cast<uint8_t>(~(1u << MECH_SYNC_DETECTED));
                        else
                            value |= static_cast<uint8_t>(1u << MECH_SYNC_DETECTED);
                    }
                }
            }

            return value;
        }

        case 0x01: // ORA/IRA - Port A
        {
            const uint8_t ddrA = registers.ddrA;

            uint8_t value =
                static_cast<uint8_t>(
                    (registers.oraIRA & ddrA) |
                    (portAPins & static_cast<uint8_t>(~ddrA))
                );

            if (viaRole == DriveVIA6522::VIARole::VIA1_IECBus)
            {
                if (auto* drive = dynamic_cast<D1571*>(parentPeripheral))
                {
                    // Track 0 sensor.
                    if ((ddrA & (1u << PORTA_TRACK0_SENSOR)) == 0)
                    {
                        const bool atTrack0 = drive->isTrack0();

                        if (atTrack0)
                            value &= static_cast<uint8_t>(~(1u << PORTA_TRACK0_SENSOR));
                        else
                            value |= static_cast<uint8_t>(1u << PORTA_TRACK0_SENSOR);
                    }

                    // Unused/pulled-high bits.
                    if ((ddrA & (1u << PORTA_UNUSED3)) == 0)
                        value |= static_cast<uint8_t>(1u << PORTA_UNUSED3);

                    if ((ddrA & (1u << PORTA_UNUSED4)) == 0)
                        value |= static_cast<uint8_t>(1u << PORTA_UNUSED4);

                    // Byte ready input.
                    if ((ddrA & (1u << PORTA_BYTE_READY)) == 0)
                    {
                        const bool byteReadyLow = drive->getByteReadyLow();

                        if (byteReadyLow)
                            value &= static_cast<uint8_t>(~(1u << PORTA_BYTE_READY));
                        else
                            value |= static_cast<uint8_t>(1u << PORTA_BYTE_READY);
                    }
                }
            }
            else if (viaRole == DriveVIA6522::VIARole::VIA2_Mechanics)
            {
                value =
                    static_cast<uint8_t>(
                        (registers.oraIRA & ddrA) |
                        (mechDataLatch & static_cast<uint8_t>(~ddrA))
                    );

                // Reading Port A consumes pending disk byte.
                if (mechBytePending)
                    mechBytePending = false;
            }

            clearIFR(IFR_CA1);
            return value;
        }

        case 0x02: // DDRB
            return registers.ddrB;

        case 0x03: // DDRA
            return registers.ddrA;

        case 0x0A: // SR
        {
            clearIFR(IFR_SR);

            if (viaRole == DriveVIA6522::VIARole::VIA2_Mechanics)
                return mechDataLatch;

            return registers.serialShift;
        }

        case 0x0F: // ORA/IRA no handshake
        {
            const uint8_t ddrA = registers.ddrA;

            uint8_t value =
                static_cast<uint8_t>(
                    (registers.oraIRA & ddrA) |
                    (portAPins & static_cast<uint8_t>(~ddrA))
                );

            if (viaRole == DriveVIA6522::VIARole::VIA1_IECBus)
            {
                if (auto* drive = dynamic_cast<D1571*>(parentPeripheral))
                {
                    if ((ddrA & (1u << PORTA_TRACK0_SENSOR)) == 0)
                    {
                        const bool atTrack0 = drive->isTrack0();

                        if (atTrack0)
                            value &= static_cast<uint8_t>(~(1u << PORTA_TRACK0_SENSOR));
                        else
                            value |= static_cast<uint8_t>(1u << PORTA_TRACK0_SENSOR);
                    }

                    if ((ddrA & (1u << PORTA_UNUSED3)) == 0)
                        value |= static_cast<uint8_t>(1u << PORTA_UNUSED3);

                    if ((ddrA & (1u << PORTA_UNUSED4)) == 0)
                        value |= static_cast<uint8_t>(1u << PORTA_UNUSED4);

                    if ((ddrA & (1u << PORTA_BYTE_READY)) == 0)
                    {
                        const bool byteReadyLow = drive->getByteReadyLow();

                        if (byteReadyLow)
                            value &= static_cast<uint8_t>(~(1u << PORTA_BYTE_READY));
                        else
                            value |= static_cast<uint8_t>(1u << PORTA_BYTE_READY);
                    }
                }
            }
            else if (viaRole == DriveVIA6522::VIARole::VIA2_Mechanics)
            {
                // No handshake: do not consume mechBytePending and do not clear CA1.
                value =
                    static_cast<uint8_t>(
                        (registers.oraIRA & ddrA) |
                        (mechDataLatch & static_cast<uint8_t>(~ddrA))
                    );
            }

            return value;
        }

        default:
            return 0xFF;
    }
}

void D1571VIA::writeRegister(uint16_t address, uint8_t value)
{
    address &= 0x0F;

    if (writeTimerRegister(address, value))
        return;

    if (writeInterruptRegister(address, value))
        return;

    if (writeControlRegister(address, value))
        return;

    switch (address)
    {
        case 0x00: // ORB/IRB - Port B
        {
            const uint8_t prevORB = registers.orbIRB;
            registers.orbIRB = value;

            if (viaRole == DriveVIA6522::VIARole::VIA1_IECBus)
            {
                updateIECOutputsFromPortB();
            }
            else if (viaRole == DriveVIA6522::VIARole::VIA2_Mechanics)
            {
                if (auto* drive = dynamic_cast<D1571*>(parentPeripheral))
                {
                    const uint8_t ddrB = registers.ddrB;

                    // Stepper phase.
                    const uint8_t phaseMask = 0x03;

                    const uint8_t oldPhase =
                        static_cast<uint8_t>(prevORB & phaseMask);

                    const uint8_t newPhase =
                        static_cast<uint8_t>(registers.orbIRB & phaseMask);

                    if (oldPhase != newPhase)
                    {
#ifdef Debug
                        std::cout << "[VIA2] step phase "
                                  << int(oldPhase)
                                  << " -> "
                                  << int(newPhase)
                                  << "\n";
#endif
                        drive->onStepperPhaseChange(oldPhase, newPhase);
                    }

                    // Motor.
                    if (ddrB & static_cast<uint8_t>(1u << MECH_SPINDLE_MOTOR))
                    {
                        const bool enable =
                            (registers.orbIRB &
                             static_cast<uint8_t>(1u << MECH_SPINDLE_MOTOR)) != 0;

                        if (enable)
                            drive->startMotor();
                        else
                            drive->stopMotor();
                    }

                    // LED.
                    if (ddrB & static_cast<uint8_t>(1u << MECH_LED))
                    {
                        const bool on =
                            (registers.orbIRB &
                             static_cast<uint8_t>(1u << MECH_LED)) != 0;

                        setLed(on);
                    }

                    // Density.
                    const uint8_t densityMask =
                        static_cast<uint8_t>(
                            (1u << MECH_DENSITY_BIT0) |
                            (1u << MECH_DENSITY_BIT1)
                        );

                    if (ddrB & densityMask)
                    {
                        const uint8_t orb = registers.orbIRB;

                        const uint8_t code =
                            static_cast<uint8_t>(
                                ((orb >> MECH_DENSITY_BIT0) & 0x01) |
                                (((orb >> MECH_DENSITY_BIT1) & 0x01) << 1)
                            );

                        drive->setDensityCode(code);
                    }
                }
            }

            break;
        }

        case 0x01: // ORA/IRA - Port A
        {
            registers.oraIRA = value;
            applyPortAOutputs(value);
            break;
        }

        case 0x02: // DDRB
        {
            registers.ddrB = value;

            const uint8_t orb = registers.orbIRB;

            if (viaRole == DriveVIA6522::VIARole::VIA1_IECBus)
            {
                updateIECOutputsFromPortB();
            }
            else if (viaRole == DriveVIA6522::VIARole::VIA2_Mechanics)
            {
                if (auto* drive = dynamic_cast<D1571*>(parentPeripheral))
                {
                    // Motor.
                    if (value & static_cast<uint8_t>(1u << MECH_SPINDLE_MOTOR))
                    {
                        const bool enable =
                            (orb & static_cast<uint8_t>(1u << MECH_SPINDLE_MOTOR)) != 0;

                        if (enable)
                            drive->startMotor();
                        else
                            drive->stopMotor();
                    }

                    // LED.
                    if (value & static_cast<uint8_t>(1u << MECH_LED))
                    {
                        const bool on =
                            (orb & static_cast<uint8_t>(1u << MECH_LED)) != 0;

                        setLed(on);
                    }

                    // Density.
                    const uint8_t densityMask =
                        static_cast<uint8_t>(
                            (1u << MECH_DENSITY_BIT0) |
                            (1u << MECH_DENSITY_BIT1)
                        );

                    if (value & densityMask)
                    {
                        const uint8_t code =
                            static_cast<uint8_t>(
                                ((orb >> MECH_DENSITY_BIT0) & 0x01) |
                                (((orb >> MECH_DENSITY_BIT1) & 0x01) << 1)
                            );

                        drive->setDensityCode(code);
                    }
                }
            }

            break;
        }

        case 0x03: // DDRA
        {
            registers.ddrA = value;

            applyPortAOutputs(registers.oraIRA);

            if (viaRole == DriveVIA6522::VIARole::VIA2_Mechanics)
                recomputeDiskWriteGate();

            break;
        }

        case 0x0A: // SR
        {
            registers.serialShift = value;
            clearIFR(IFR_SR);
            break;
        }

        case 0x0F: // ORA/IRA no handshake
        {
            registers.oraIRA = value;
            registers.oraIRANoHandshake = value;
            applyPortAOutputs(value);
            break;
        }

        default:
            break;
    }
}

void D1571VIA::diskByteFromMedia(uint8_t byte, bool inSync)
{
    if (viaRole != DriveVIA6522::VIARole::VIA2_Mechanics)
        return;

    setSyncDetected(inSync);

    if (inSync)
    {
        mechBytePending = false;
        clearIFR(IFR_CA1);
        return;
    }

    mechDataLatch = byte;
    mechBytePending = true;

    triggerInterrupt(IFR_CA1);

    if (parentPeripheral)
    {
        if (auto* drive = dynamic_cast<D1571*>(parentPeripheral))
        {
            if (auto* cpu = drive->getDriveCPU())
                cpu->pulseSO();
        }
    }
}

void D1571VIA::setIECInputLines(bool atnLow, bool clkLow, bool dataLow)
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

    if (viaRole == DriveVIA6522::VIARole::VIA1_IECBus && (newAtnLow != prevAtnLow))
        updateIECOutputsFromPortB();
}

void D1571VIA::updateIECOutputsFromPortB()
{
    if (viaRole != DriveVIA6522::VIARole::VIA1_IECBus) return;

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
    bool atnAsserted = ((portBPins & (1u << IEC_ATN_IN_BIT)) != 0);

    bool atnAckAuto = false;
    if (ddrB & (1u << IEC_ATN_ACK_BIT))
        atnAckAuto = ((orb & (1u << IEC_ATN_ACK_BIT)) == 0);

    if (atnAsserted && atnAckAuto)
        dataLow = true; // force DATA low as the acknowledge

    d1571->peripheralAssertData(dataLow);
    d1571->peripheralAssertClk(clkLow);
}

bool D1571VIA::checkIRQActive() const
{
    uint8_t active = registers.interruptEnable & registers.interruptFlag & 0x7F;
    return active != 0;
}

void D1571VIA::onClkEdge(bool rising, bool falling)
{
    if (viaRole != DriveVIA6522::VIARole::VIA1_IECBus)
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
    if (viaRole != DriveVIA6522::VIARole::VIA2_Mechanics)
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

void D1571VIA::clearMechBytePending()
{
    mechBytePending = false;
    clearIFR(IFR_CA1);
}

void D1571VIA::pulseWriteByteReady()
{
    if (viaRole != DriveVIA6522::VIARole::VIA2_Mechanics)
        return;

    mechBytePending = true;

    triggerInterrupt(IFR_CA1);

    if (parentPeripheral)
    {
        if (auto* drive = dynamic_cast<D1571*>(parentPeripheral))
        {
            if (auto* cpu = drive->getDriveCPU())
                cpu->pulseSO();
        }
    }
}

void D1571VIA::recomputeDiskWriteGate()
{
    if (viaRole != DriveVIA6522::VIARole::VIA2_Mechanics)
        return;

    auto* drive = dynamic_cast<D1571*>(parentPeripheral);
    if (!drive)
        return;

    const uint8_t pcr = registers.peripheralControlRegister;
    const uint8_t ca2Mode = (pcr >> 1) & 0x07;
    const uint8_t cb2Mode = (pcr >> 5) & 0x07;

    const bool portAOutput = (registers.ddrA == 0xFF);

    // Same phase that worked for the 1541:
    // PCR=$CE, DDRA=$FF
    const bool pcrWritePhase =
        (pcr == 0xCE) || (ca2Mode == 0b111 && cb2Mode == 0b110);

    const bool gate = portAOutput && pcrWritePhase;

#ifdef Debug
    static bool lastGate = false;
    static uint8_t lastPcr = 0xFF;
    static uint8_t lastDdra = 0xFF;

    if (gate != lastGate || pcr != lastPcr || registers.ddrA != lastDdra)
    {
        if (gate || lastGate || registers.ddrA == 0xFF)
        {
            std::cout << "[D1571:VIA2:GATE] "
                      << "PCR=$" << std::hex << std::uppercase << int(pcr)
                      << " DDRA=$" << int(registers.ddrA)
                      << std::dec
                      << " CA2=" << int(ca2Mode)
                      << " CB2=" << int(cb2Mode)
                      << " gate=" << (gate ? 1 : 0)
                      << "\n";
        }

        lastGate = gate;
        lastPcr = pcr;
        lastDdra = registers.ddrA;
    }
#endif

    drive->setDiskWriteGate(gate);
}

void D1571VIA::applyPortAOutputs(uint8_t value)
{
    const uint8_t ddrA = registers.ddrA;

    if (viaRole == DriveVIA6522::VIARole::VIA1_IECBus)
    {
        if (auto* drive = dynamic_cast<D1571*>(parentPeripheral))
        {
            if (ddrA & (1u << PORTA_FSM_DIRECTION))
            {
                const bool output = (value & (1u << PORTA_FSM_DIRECTION)) != 0;
                drive->setBusDriversEnabled(output);
                updateIECOutputsFromPortB();
            }

            if (ddrA & (1u << PORTA_RWSIDE_SELECT))
            {
                const bool side1 = (value & (1u << PORTA_RWSIDE_SELECT)) != 0;
                drive->setHeadSide(side1);
            }

            if (ddrA & (1u << PORTA_PHI2_CLKSEL))
            {
                const bool twoMHz = (value & (1u << PORTA_PHI2_CLKSEL)) != 0;
                drive->setBurstClock2MHz(twoMHz);
            }
        }
    }
    else if (viaRole == DriveVIA6522::VIARole::VIA2_Mechanics)
    {
        if (auto* drive = dynamic_cast<D1571*>(parentPeripheral))
            drive->onVIA2PortAWrite(value, registers.ddrA);
    }
}

void D1571VIA::onPCRChanged(uint8_t oldValue, uint8_t newValue)
{
    (void)oldValue;
    (void)newValue;

    if (viaRole == DriveVIA6522::VIARole::VIA2_Mechanics)
        recomputeDiskWriteGate();
}
