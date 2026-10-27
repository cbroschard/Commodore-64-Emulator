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
    srShiftInMode(false),
    ledOn(false),
    syncDetected(false),
    mechDataLatch(0xFF),
    mechReadBytePending(false),
    mechWriteByteRequest(false),
    byteReadyActive(false)
{
    reset();
}

D1571VIA::~D1571VIA() = default;

void D1571VIA::saveState(StateWriter& wrtr) const
{
    wrtr.writeU32(3);

    saveVIAState(wrtr);

    // Serial shift runtime
    wrtr.writeU8(srShiftReg);
    wrtr.writeU8(srBitCount);
    wrtr.writeBool(srShiftInMode);

    // Mechanical signals
    wrtr.writeBool(ledOn);
    wrtr.writeBool(syncDetected);
    wrtr.writeU8(mechDataLatch);
    wrtr.writeBool(mechReadBytePending);
    wrtr.writeBool(mechWriteByteRequest);
    wrtr.writeBool(byteReadyActive);

    // VIA control pin levels (introduced in version 3)
    wrtr.writeBool(ca1Level);
    wrtr.writeBool(ca2Level);
    wrtr.writeBool(cb1Level);
    wrtr.writeBool(cb2Level);
}

bool D1571VIA::loadState(StateReader& rdr)
{
    uint32_t ver = 0;

    if (!rdr.readU32(ver))
        return false;

    if (ver < 1 || ver > 3)
        return false;

    if (!loadVIAState(rdr))
        return false;

    // Serial shift runtime
    if (!rdr.readU8(srShiftReg))
        return false;

    if (!rdr.readU8(srBitCount))
        return false;

    if (!rdr.readBool(srShiftInMode))
        return false;

    // Mechanical signals
    if (!rdr.readBool(ledOn))
        return false;

    if (!rdr.readBool(syncDetected))
        return false;

    if (!rdr.readU8(mechDataLatch))
        return false;

    if (ver == 1)
    {
        bool oldMechBytePending = false;

        if (!rdr.readBool(oldMechBytePending))
            return false;

        mechReadBytePending  = oldMechBytePending;
        mechWriteByteRequest = false;
        byteReadyActive      = oldMechBytePending;
    }
    else
    {
        if (!rdr.readBool(mechReadBytePending))
            return false;

        if (!rdr.readBool(mechWriteByteRequest))
            return false;

        if (!rdr.readBool(byteReadyActive))
            return false;
    }

    // VIA control pin levels (version 3+)
    if (ver >= 3)
    {
        bool savedCA1 = false;
        bool savedCA2 = false;
        bool savedCB1 = false;
        bool savedCB2 = false;

        if (!rdr.readBool(savedCA1))
            return false;

        if (!rdr.readBool(savedCA2))
            return false;

        if (!rdr.readBool(savedCB1))
            return false;

        if (!rdr.readBool(savedCB2))
            return false;

        // Direct assignment prevents synthetic interrupts during load.
        ca1Level = savedCA1;
        ca2Level = savedCA2;
        cb1Level = savedCB1;
        cb2Level = savedCB2;
    }
    else
    {
        // Legacy versions 1 and 2 did not serialize CA/CB pin levels.
        // Preserve the historical reset baseline.
        ca1Level = false;
        ca2Level = false;
        cb1Level = false;
        cb2Level = false;
    }

    // Post-restore fixups / derived state
    applyPortAOutputs(registers.oraIRA);

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

    // Mechanics
    ledOn                   = false;
    syncDetected            = false;
    mechDataLatch           = 0xFF;
    mechReadBytePending     = false;
    mechWriteByteRequest    = false;

    byteReadyActive         = false;

    // Serial shift
    srShiftReg              = 0;
    srBitCount              = 0;
    srShiftInMode           = false;

    if (viaRole == DriveVIA6522::VIARole::VIA1_IECBus)
    {
        portBPins &= static_cast<uint8_t>(~(
            (1u << IEC_ATN_IN_BIT) |
            (1u << IEC_CLK_IN_BIT) |
            (1u << IEC_DATA_IN_BIT)
        ));
    }

    applyPortAOutputs(registers.oraIRA);

    if (viaRole == DriveVIA6522::VIARole::VIA1_IECBus)
        updateIECOutputsFromPortB();

    if (viaRole == DriveVIA6522::VIARole::VIA2_Mechanics)
        recomputeDiskWriteGate();
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

    uint8_t portValue = 0xFF;

    if (readCommonPortRegister(address, portValue))
        return portValue;

    uint8_t shiftValue = 0xFF;

    if (readCommonShiftRegister(address, shiftValue))
        return shiftValue;

    switch (address)
    {
        case 0x01: // ORA/IRA - Port A
        {
            // Sample the physical hardware inputs first.
            const uint8_t inputPins = samplePortAInputs();

            // Shared DDR masking and CA1/CA2 acknowledgement.
            const uint8_t value = readPortARegister(inputPins, true);

            // IMPORTANT: Consume the disk byte AFTER the read.
            // This ordering is required by our working 1571 path.
            if (viaRole == VIARole::VIA2_Mechanics &&
                mechReadBytePending)
            {
                mechReadBytePending = false;
                byteReadyActive = false;
            }

            return value;
        }

        case 0x0F: // ORA/IRA - Port A, no handshake
        {
            const uint8_t ddrA = registers.ddrA;
            uint8_t value = readPortAPins(portAPins);

            if (viaRole == VIARole::VIA1_IECBus)
            {
                if (auto* drive = dynamic_cast<D1571*>(parentPeripheral))
                {
                    // Track-zero sensor, active low.
                    if ((ddrA & (1u << PORTA_TRACK0_SENSOR)) == 0)
                    {
                        if (drive->isTrack0())
                            value &= static_cast<uint8_t>(~(1u << PORTA_TRACK0_SENSOR));
                        else
                            value |= static_cast<uint8_t>(1u << PORTA_TRACK0_SENSOR);
                    }

                    // Unused bits pulled high.
                    if ((ddrA & (1u << PORTA_UNUSED3)) == 0)
                        value |= static_cast<uint8_t>(1u << PORTA_UNUSED3);

                    if ((ddrA & (1u << PORTA_UNUSED4)) == 0)
                        value |= static_cast<uint8_t>(1u << PORTA_UNUSED4);

                    // Byte-ready input, active low.
                    if ((ddrA & (1u << PORTA_BYTE_READY)) == 0)
                    {
                        if (drive->getByteReadyLow())
                            value &= static_cast<uint8_t>(~(1u << PORTA_BYTE_READY));
                        else
                            value |= static_cast<uint8_t>(1u << PORTA_BYTE_READY);
                    }
                }
            }
            else if (viaRole == VIARole::VIA2_Mechanics)
            {
                // Read current disk-data latch.
                // Do not consume the pending disk byte.
                value = readPortAPins(mechDataLatch);
            }

            const uint8_t sampledValue = readPortARegister(value, false);
            uint8_t result = 0xFF;

            if (readCommonPortANoHandshake(address, sampledValue, result))
                return result;

            return sampledValue;
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

    if (writeCommonPortRegister(address, value))
        return;

    switch (address)
    {
        case 0x0A: // SR
        {
            registers.serialShift = value;
            clearIFR(IFR_SR);
            break;
        }

        case 0x0F: // ORA/IRA no handshake
        {
            const uint8_t oldValue = registers.oraIRA;

            writePortALatch(value, true);
            onPortAOutputChanged(oldValue, value);
            acknowledgePortA();

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
        mechReadBytePending = false;
        byteReadyActive = false;

        clearIFR(IFR_CA1);
        return;
    }

    const bool wasPending = mechReadBytePending;

    // The rotating disk continues updating the data latch even if
    // the CPU has not yet consumed the previous byte.
    mechDataLatch = byte;
    mechReadBytePending = true;
    byteReadyActive = true;

    // CA1 represents the beginning of a new pending-byte condition.
    if (!wasPending)
        triggerInterrupt(IFR_CA1);

    // In 1 MHz / 1541-compatible operation, every completed
    // GCR byte drives SO.
    //
    // Native 2 MHz operation uses the 1571 BYTE READY path instead.
    if (parentPeripheral)
    {
        if (auto* drive = dynamic_cast<D1571*>(parentPeripheral))
        {
            if (!drive->isTwoMHzMode())
            {
                if (auto* cpu = drive->getDriveCPU())
                    cpu->pulseSO();
            }
        }
    }
}

void D1571VIA::setIECInputLines(bool atnLow, bool clkLow, bool dataLow)
{
    const bool prevAtnLow = ((portBPins & (1u << IEC_ATN_IN_BIT)) != 0);
    const bool prevClkLow = ((portBPins & (1u << IEC_CLK_IN_BIT)) != 0);

    uint8_t pins = portBPins;

    if (dataLow)
        pins |= static_cast<uint8_t>(1u << IEC_DATA_IN_BIT);
    else
        pins &= static_cast<uint8_t>(~(1u << IEC_DATA_IN_BIT));

    if (clkLow)
        pins |= static_cast<uint8_t>(1u << IEC_CLK_IN_BIT);
    else
        pins &= static_cast<uint8_t>(~(1u << IEC_CLK_IN_BIT));

    if (atnLow)
        pins |= static_cast<uint8_t>(1u << IEC_ATN_IN_BIT);
    else
        pins &= static_cast<uint8_t>(~(1u << IEC_ATN_IN_BIT));

    portBPins = pins;

    const bool newAtnLow = ((portBPins & (1u << IEC_ATN_IN_BIT)) != 0);
    const bool newClkLow = ((portBPins & (1u << IEC_CLK_IN_BIT)) != 0);

    const bool atnFallingEdge = !prevAtnLow && newAtnLow;
    const bool atnRisingEdge  = prevAtnLow && !newAtnLow;

    const bool clkRisingEdge  = prevClkLow && !newClkLow;
    const bool clkFallingEdge = !prevClkLow && newClkLow;

    if (viaRole == DriveVIA6522::VIARole::VIA1_IECBus)
    {
        if (clkRisingEdge || clkFallingEdge)
            onClkEdge(clkRisingEdge, clkFallingEdge);

        if (atnFallingEdge || atnRisingEdge || clkRisingEdge)
            updateIECOutputsFromPortB();
    }
}

void D1571VIA::updateIECOutputsFromPortB()
{
    if (viaRole != DriveVIA6522::VIARole::VIA1_IECBus)
        return;

    auto* d1571 = dynamic_cast<D1571*>(parentPeripheral);

    if (!d1571)
        return;

    const uint8_t orb  = registers.orbIRB;
    const uint8_t ddrB = registers.ddrB;

    bool dataLow = false;
    bool clkLow  = false;

    // PB1: DATA OUT.
    if (ddrB & (1u << IEC_DATA_OUT_BIT))
        dataLow = (orb & (1u << IEC_DATA_OUT_BIT)) != 0;

    // PB3: CLK OUT.
    if (ddrB & (1u << IEC_CLK_OUT_BIT))
        clkLow = (orb & (1u << IEC_CLK_OUT_BIT)) != 0;

    // PB4: ATNA.
    //
    // Hardware behavior is combinational:
    // DATA acknowledge is asserted whenever ATN and ATNA disagree.
    //
    // If PB4 is configured as an input, treat the undriven pin as HIGH.
    const bool atnaHigh = (ddrB & (1u << IEC_ATN_ACK_BIT)) ? ((orb & (1u << IEC_ATN_ACK_BIT)) != 0) : true;

    // PB7 is the physical ATN input sampled by VIA1.
    // A set bit represents an asserted (LOW) IEC ATN line.
    const bool atnLow = (portBPins & (1u << IEC_ATN_IN_BIT)) != 0;

    // Preserve the existing 1571 ATN acknowledgment polarity.
    const bool atnAckDataLow = (atnaHigh != atnLow);

    const bool finalDataLow = dataLow || atnAckDataLow;

    d1571->peripheralAssertData(finalDataLow);
    d1571->peripheralAssertClk(clkLow);
}

void D1571VIA::onClkEdge(bool rising, bool falling)
{
    (void)falling;

    if (viaRole != VIARole::VIA1_IECBus)
        return;

    srShiftInMode = (registers.auxControlRegister & 0x0C) == 0x04;

    if (rising && srShiftInMode)
    {
        bool dataLow = false;

        if (auto* drive = dynamic_cast<D1571*>(parentPeripheral))
            dataLow = drive->getDataLineLow();

        shiftInBit(!dataLow);
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
        m.motorOn = (orb & (1u << MECH_SPINDLE_MOTOR)) != 0;
    else
        m.motorOn = false; // or leave as-is

    if (ddrB & (1u << MECH_LED))
        m.ledOn = (orb & (1u << MECH_LED)) != 0;
    else
        m.ledOn = false;

    // Density bits: PB5/PB6
    uint8_t code = 0;

    if (ddrB & (1u << MECH_DENSITY_BIT0))
        code |= (orb >> MECH_DENSITY_BIT0) & 0x01;

    if (ddrB & (1u << MECH_DENSITY_BIT1))
        code |= ((orb >> MECH_DENSITY_BIT1) & 0x01) << 1;

    m.densityCode = code;

    return m;
}

void D1571VIA::consumeWriteByteRequest()
{
    if (viaRole != DriveVIA6522::VIARole::VIA2_Mechanics)
        return;

    mechWriteByteRequest = false;
    byteReadyActive = false;
}

void D1571VIA::clearMechBytePending()
{
    mechReadBytePending = false;
    mechWriteByteRequest = false;
    byteReadyActive = false;

    clearIFR(IFR_CA1);
}

void D1571VIA::pulseWriteByteReady()
{
    if (viaRole != DriveVIA6522::VIARole::VIA2_Mechanics)
        return;

    mechWriteByteRequest = true;
    byteReadyActive = true;

    triggerInterrupt(IFR_CA1);

    if (parentPeripheral)
    {
        if (auto* drive = dynamic_cast<D1571*>(parentPeripheral))
        {
            // In 1 MHz / 1541-compatible mode, BYTE READY
            // is routed to the CPU SO input.
            //
            // Native 2 MHz mode observes BYTE READY through
            // the 1571 hardware path instead.
            if (!drive->isTwoMHzMode())
            {
                if (auto* cpu = drive->getDriveCPU())
                    cpu->pulseSO();
            }
        }
    }
}

void D1571VIA::primeIECAtnLevel(bool atnLow)
{
    if (viaRole != VIARole::VIA1_IECBus)
        return;

    // Synchronize the physical CA1 level without
    // generating a synthetic ATN interrupt.
    ca1Level = atnLow;
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

    // PCR=$CE, DDRA=$FF
    const bool pcrWritePhase = (pcr == 0xCE) || (ca2Mode == 0b111 && cb2Mode == 0b110);

    const bool gate = portAOutput && pcrWritePhase;

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
}

void D1571VIA::onAttachedToPeripheral()
{
    applyPortAOutputs(registers.oraIRA);

    if (viaRole == DriveVIA6522::VIARole::VIA1_IECBus)
        updateIECOutputsFromPortB();

    if (viaRole == DriveVIA6522::VIARole::VIA2_Mechanics)
        recomputeDiskWriteGate();
}

void D1571VIA::onPCRChanged(uint8_t oldValue, uint8_t newValue)
{
    (void)oldValue;
    (void)newValue;

    if (viaRole == DriveVIA6522::VIARole::VIA2_Mechanics)
        recomputeDiskWriteGate();
}

uint8_t D1571VIA::samplePortAInputs()
{
    uint8_t pins = portAPins;

    // VIA2 reads from the GCR disk-data latch.
    if (viaRole == VIARole::VIA2_Mechanics)
        return mechDataLatch;

    if (viaRole != VIARole::VIA1_IECBus)
        return pins;

    auto* drive = dynamic_cast<D1571*>(parentPeripheral);

    if (!drive)
        return pins;

    // Track-zero sensor: active low.
    if ((registers.ddrA & (1u << PORTA_TRACK0_SENSOR)) == 0)
    {
        if (drive->isTrack0())
            pins &= static_cast<uint8_t>(~(1u << PORTA_TRACK0_SENSOR));
        else
            pins |= static_cast<uint8_t>(1u << PORTA_TRACK0_SENSOR);
    }

    // Unused input pins are pulled high.
    if ((registers.ddrA & (1u << PORTA_UNUSED3)) == 0)
        pins |= static_cast<uint8_t>(1u << PORTA_UNUSED3);

    if ((registers.ddrA & (1u << PORTA_UNUSED4)) == 0)
        pins |= static_cast<uint8_t>(1u << PORTA_UNUSED4);

    // Byte-ready signal: active low.
    if ((registers.ddrA & (1u << PORTA_BYTE_READY)) == 0)
    {
        if (drive->getByteReadyLow())
            pins &= static_cast<uint8_t>(~(1u << PORTA_BYTE_READY));
        else
            pins |= static_cast<uint8_t>(1u << PORTA_BYTE_READY);
    }

    return pins;
}

uint8_t D1571VIA::samplePortBInputs()
{
    uint8_t pins = portBPins;
    const uint8_t ddrB = registers.ddrB;

    if (viaRole == VIARole::VIA1_IECBus)
    {
        if (auto* drive = dynamic_cast<D1571*>(parentPeripheral))
        {
            int offset = drive->getDeviceNumber() - 8;

            if (offset < 0 || offset > 3)
                offset = 0;

            if ((ddrB & (1u << IEC_DEV_BIT0)) == 0)
            {
                if (offset & 0x01)
                    pins |= static_cast<uint8_t>(1u << IEC_DEV_BIT0);
                else
                    pins &= static_cast<uint8_t>(~(1u << IEC_DEV_BIT0));
            }

            if ((ddrB & (1u << IEC_DEV_BIT1)) == 0)
            {
                if (offset & 0x02)
                    pins |= static_cast<uint8_t>(1u << IEC_DEV_BIT1);
                else
                    pins &= static_cast<uint8_t>(~(1u << IEC_DEV_BIT1));
            }
        }
    }
    else if (viaRole == VIARole::VIA2_Mechanics)
    {
        if (auto* drive = dynamic_cast<D1571*>(parentPeripheral))
        {
            if ((ddrB & (1u << MECH_WRITE_PROTECT)) == 0)
            {
                if (drive->fdcIsWriteProtected())
                    pins &= static_cast<uint8_t>(~(1u << MECH_WRITE_PROTECT));
                else
                    pins |= static_cast<uint8_t>(1u << MECH_WRITE_PROTECT);
            }

            if ((ddrB & (1u << MECH_SYNC_DETECTED)) == 0)
            {
                if (isSyncDetected())
                    pins &= static_cast<uint8_t>(~(1u << MECH_SYNC_DETECTED));
                else
                    pins |= static_cast<uint8_t>(1u << MECH_SYNC_DETECTED);
            }
        }
    }

    return pins;
}

void D1571VIA::onPortAOutputChanged(uint8_t oldValue, uint8_t newValue)
{
    (void)oldValue;

    applyPortAOutputs(newValue);

    if (viaRole == DriveVIA6522::VIARole::VIA2_Mechanics)
        consumeWriteByteRequest();
}

void D1571VIA::onPortBOutputChanged(uint8_t oldValue, uint8_t newValue)
{
    if (viaRole == DriveVIA6522::VIARole::VIA1_IECBus)
    {
        updateIECOutputsFromPortB();
        return;
    }

    if (viaRole != DriveVIA6522::VIARole::VIA2_Mechanics)
        return;

    auto* drive = dynamic_cast<D1571*>(parentPeripheral);
    if (!drive)
        return;

    const uint8_t ddrB = registers.ddrB;

    // Stepper phase
    const uint8_t phaseMask = 0x03;

    const uint8_t oldPhase = static_cast<uint8_t>(oldValue & phaseMask);
    const uint8_t newPhase = static_cast<uint8_t>(newValue & phaseMask);

    if (oldPhase != newPhase)
        drive->onStepperPhaseChange(oldPhase, newPhase);

    // Spindle motor
    if (ddrB & static_cast<uint8_t>(1u << MECH_SPINDLE_MOTOR))
    {
        const bool enable = (newValue & static_cast<uint8_t>(1u << MECH_SPINDLE_MOTOR)) != 0;

        if (enable)
            drive->startMotor();
        else
            drive->stopMotor();
    }

    // Drive LED
    if (ddrB & static_cast<uint8_t>(1u << MECH_LED))
    {
        const bool on = (newValue & static_cast<uint8_t>(1u << MECH_LED)) != 0;
        setLed(on);
    }

    // Density selection
    const uint8_t densityMask = static_cast<uint8_t>((1u << MECH_DENSITY_BIT0) | (1u << MECH_DENSITY_BIT1));

    if (ddrB & densityMask)
    {
        const uint8_t code = static_cast<uint8_t>(((newValue >> MECH_DENSITY_BIT0) & 0x01) |
                (((newValue >> MECH_DENSITY_BIT1) & 0x01) << 1));

        drive->setDensityCode(code);
    }
}

void D1571VIA::onDDRAChanged(uint8_t oldDDR, uint8_t newDDR)
{
    (void)oldDDR;
    (void)newDDR;

    // DDR changes alter which Port A signals are driven.
    applyPortAOutputs(registers.oraIRA);

    if (viaRole == DriveVIA6522::VIARole::VIA2_Mechanics)
        recomputeDiskWriteGate();
}

void D1571VIA::onDDRBChanged(uint8_t oldDDR, uint8_t newDDR)
{
    (void)oldDDR;

    if (viaRole == DriveVIA6522::VIARole::VIA1_IECBus)
    {
        updateIECOutputsFromPortB();
        return;
    }

    if (viaRole != DriveVIA6522::VIARole::VIA2_Mechanics)
        return;

    auto* drive = dynamic_cast<D1571*>(parentPeripheral);
    if (!drive)
        return;

    const uint8_t orb = registers.orbIRB;

    // Spindle motor
    if (newDDR & static_cast<uint8_t>(1u << MECH_SPINDLE_MOTOR))
    {
        const bool enable =
            (orb & static_cast<uint8_t>(1u << MECH_SPINDLE_MOTOR)) != 0;

        if (enable)
            drive->startMotor();
        else
            drive->stopMotor();
    }

    // Drive LED
    if (newDDR & static_cast<uint8_t>(1u << MECH_LED))
    {
        const bool on = (orb & static_cast<uint8_t>(1u << MECH_LED)) != 0;
        setLed(on);
    }

    // Density selection
    const uint8_t densityMask = static_cast<uint8_t>((1u << MECH_DENSITY_BIT0) | (1u << MECH_DENSITY_BIT1));

    if (newDDR & densityMask)
    {
        const uint8_t code = static_cast<uint8_t>(((orb >> MECH_DENSITY_BIT0) & 0x01) | (((orb >> MECH_DENSITY_BIT1) & 0x01) << 1));
        drive->setDensityCode(code);
    }
}

uint8_t D1571VIA::sampleShiftRegisterRead()
{
    return registers.serialShift;
}
