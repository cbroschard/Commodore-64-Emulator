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
    srShiftReg(0),
    srBitCount(0),
    srShiftInMode(false),
    iecRxPending(false),
    iecRxByte(0),
    ledOn(false),
    mechDataLatch(0xFF),
    mechBytePending(false),
    atnAckArmed(false),
    atnAckLatch(false),
    prevAtnAckClear(false),
    iecInputPrimed(false),
    busAtnLow(false),
    busClkLow(false),
    busDataLow(false),
    srCount(0)
{
    reset();
}

D1541VIA::~D1541VIA() = default;

void D1541VIA::saveState(StateWriter& wrtr) const
{
    wrtr.writeU32(2);

    saveVIAState(wrtr);

    wrtr.writeBool(iecInputPrimed);
    wrtr.writeBool(busAtnLow);
    wrtr.writeBool(busClkLow);
    wrtr.writeBool(busDataLow);
    wrtr.writeBool(atnAckArmed);
    wrtr.writeBool(atnAckLatch);
    wrtr.writeBool(prevAtnAckClear);
    wrtr.writeBool(iecRxPending);
    wrtr.writeU8(iecRxByte);

    wrtr.writeU8(srShiftReg);
    wrtr.writeU8(srBitCount);
    wrtr.writeI32(srCount);
    wrtr.writeBool(srShiftInMode);

    wrtr.writeBool(ledOn);
    wrtr.writeBool(syncDetected);
    wrtr.writeU8(mechDataLatch);
    wrtr.writeBool(mechBytePending);

    wrtr.writeBool(ca1Level);
    wrtr.writeBool(ca2Level);
    wrtr.writeBool(cb1Level);
    wrtr.writeBool(cb2Level);
}

bool D1541VIA::loadState(StateReader& rdr)
{
    uint32_t ver = 0;
    if (!rdr.readU32(ver)) return false;

    if (ver != 2)
        return false;

    if (!loadVIAState(rdr))
        return false;

    // IEC state
    if (!rdr.readBool(iecInputPrimed)) return false;

    if (!rdr.readBool(busAtnLow)) return false;
    if (!rdr.readBool(busClkLow)) return false;
    if (!rdr.readBool(busDataLow)) return false;

    if (!rdr.readBool(atnAckArmed)) return false;
    if (!rdr.readBool(atnAckLatch)) return false;
    if (!rdr.readBool(prevAtnAckClear)) return false;

    if (!rdr.readBool(iecRxPending)) return false;
    if (!rdr.readU8(iecRxByte)) return false;

    // Serial shift pipeline
    if (!rdr.readU8(srShiftReg)) return false;
    if (!rdr.readU8(srBitCount)) return false;
    if (!rdr.readI32(srCount)) return false;
    if (!rdr.readBool(srShiftInMode)) return false;

    // Mechanism signals
    if (!rdr.readBool(ledOn)) return false;
    if (!rdr.readBool(syncDetected)) return false;
    if (!rdr.readU8(mechDataLatch)) return false;
    if (!rdr.readBool(mechBytePending)) return false;

    // CA/CB pin levels
    if (!rdr.readBool(ca1Level)) return false;
    if (!rdr.readBool(ca2Level)) return false;
    if (!rdr.readBool(cb1Level)) return false;
    if (!rdr.readBool(cb2Level)) return false;

    // Post-load resync
    if (viaRole == VIARole::VIA1_IECBus)
        updateIECOutputsFromPortB();

    if (viaRole == VIARole::VIA2_Mechanics)
        recomputeDiskWriteGate();

    refreshMasterBit();

    return true;
}

void D1541VIA::reset()
{
    DriveVIA6522::reset();

    iecInputPrimed                      = false;

    // Latched real bus levels
    busAtnLow                           = false;
    busClkLow                           = false;
    busDataLow                          = false;

    // Handshake
    atnAckArmed                         = false;
    atnAckLatch                         = false;
    prevAtnAckClear                     = false;

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

    // IEC Bits
    iecRxPending                        = false;
    iecRxByte                           = 0;

    portBPins &= (uint8_t)
    ~(
        (1u << IEC_ATN_IN_BIT) |
        (1u << IEC_CLK_IN_BIT) |
        (1u << IEC_DATA_IN_BIT)
    );

    if (viaRole == VIARole::VIA1_IECBus)
        updateIECOutputsFromPortB(); // forces bus release based on DDRB/ORB
}

void D1541VIA::resetShift()
{
    srShiftReg = 0;
    srBitCount = 0;
    srShiftInMode = false;
}

uint8_t D1541VIA::readRegister(uint16_t address)
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

            uint8_t value = readPortBPins(portBPins);

            acknowledgePortB();

            if (viaRole == VIARole::VIA1_IECBus)
            {
                if (auto* drive = dynamic_cast<D1541*>(parentPeripheral))
                {
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
            else if (viaRole == VIARole::VIA2_Mechanics)
            {
                if (auto* drive = dynamic_cast<D1541*>(parentPeripheral))
                {
                    if ((ddrB & (1u << MECH_WRITE_PROTECT)) == 0)
                    {
                        const bool wpLow = drive->isWriteProtected();

                        if (wpLow)
                            value &= static_cast<uint8_t>(~(1u << MECH_WRITE_PROTECT));
                        else
                            value |= static_cast<uint8_t>(1u << MECH_WRITE_PROTECT);
                    }

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

            uint8_t inputPins = portAPins;

            if (viaRole == VIARole::VIA1_IECBus)
            {
                if (auto* drive = dynamic_cast<D1541*>(parentPeripheral))
                {
                    if ((ddrA & (1u << PORTA_TRACK0_SENSOR)) == 0)
                    {
                        const bool atTrack0 = drive->isTrack0();

                        if (atTrack0)
                            inputPins &= static_cast<uint8_t>(~(1u << PORTA_TRACK0_SENSOR));
                        else
                            inputPins |= static_cast<uint8_t>(1u << PORTA_TRACK0_SENSOR);
                    }

                    if ((ddrA & (1u << PORTA_BYTE_READY)) == 0)
                    {
                        const bool byteReadyLow = drive->getByteReadyLow();

                        if (byteReadyLow)
                            inputPins &= static_cast<uint8_t>(~(1u << PORTA_BYTE_READY));
                        else
                            inputPins |= static_cast<uint8_t>(1u << PORTA_BYTE_READY);
                    }
                }
            }

            // Shared 6522 DDR-aware Port A read.
            uint8_t value = readPortAPins(inputPins);

            if (viaRole == VIARole::VIA2_Mechanics)
            {
                // VIA2 reads its data from the disk mechanism latch.
                value = readPortAPins(mechDataLatch);

                if (mechBytePending)
                {
                    if (auto* drive = dynamic_cast<D1541*>(parentPeripheral))
                        drive->onVIA2PortARead(mechDataLatch);

                    mechBytePending = false;
                }
            }

            acknowledgePortA();

            return value;
        }

        case 0x02: // DDRB
            return registers.ddrB;

        case 0x03: // DDRA
            return registers.ddrA;

        case 0x0A: // SR
        {
            clearIFR(IFR_SR);

            if (viaRole == VIARole::VIA2_Mechanics)
                return mechDataLatch;

            return registers.serialShift;
        }

        case 0x0F: // ORA/IRA no handshake
        {
            uint8_t inputPins = portAPins;

            if (viaRole == VIARole::VIA2_Mechanics)
                inputPins = mechDataLatch;

            return readPortAPins(inputPins);
        }

        default:
            return 0xFF;
    }
}

void D1541VIA::writeRegister(uint16_t address, uint8_t value)
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
            const uint8_t prevORB = writePortBLatch(value);

            // Notify the drive hardware of the new Port B output.
            onPortBOutputChanged(prevORB, value);

            acknowledgePortB();

            break;
        }

        case 0x01: // ORA/IRA - Port A
        {
            const uint8_t oldValue = registers.oraIRA;

            writePortALatch(value);
            onPortAOutputChanged(oldValue, value);
            acknowledgePortA();

            break;
        }

        case 0x02: // DDRB
        {
            const uint8_t oldDDR = registers.ddrB;

            writePortBDirection(value);

            onDDRBChanged(oldDDR, value);

            break;
        }

        case 0x03: // DDRA
        {
            const uint8_t oldDDR = registers.ddrA;

            writePortADirection(value);
            onDDRAChanged(oldDDR, value);

            break;
        }

        case 0x0A: // SR
        {
            registers.serialShift = value;
            clearIFR(IFR_SR);
            resetShift();
            break;
        }

        case 0x0F: // ORA/IRA no handshake
        {
            const uint8_t oldValue = registers.oraIRA;

            writePortALatch(value);
            onPortAOutputChanged(oldValue, value);
            acknowledgePortA();

            break;
        }

        default:
            break;
    }
}

void D1541VIA::diskByteFromMedia(uint8_t byte, bool inSync)
{
    if (viaRole != VIARole::VIA2_Mechanics)
        return;

    setSyncDetected(inSync);

    // During sync, the data separator reports SYNC on PB7.
    // Do not keep an old data byte pending through sync.
    if (inSync)
    {
        mechBytePending = false;
        clearIFR(IFR_CA1);
        return;
    }

    // Hardware-like behavior:
    // the disk continues rotating. Do not freeze the data latch just because
    // the ROM did not read the previous byte yet.
    mechDataLatch = byte;
    mechBytePending = true;

    triggerInterrupt(IFR_CA1);

    if (parentPeripheral)
    {
        if (auto* drive = dynamic_cast<D1541*>(parentPeripheral))
        {
            if (auto* cpu = drive->getDriveCPU())
                cpu->pulseSO();
        }
    }
}

void D1541VIA::setIECInputLines(bool atnLow, bool clkLow, bool dataLow)
{
    if (viaRole != VIARole::VIA1_IECBus)
        return;

    // PRIME: first time we ever see bus levels after attach/reset,
    // just latch them as baseline and update input pins — NO EDGES.
    if (!iecInputPrimed)
    {
        iecInputPrimed = true;

        busAtnLow  = atnLow;
        busClkLow  = clkLow;
        busDataLow = dataLow;

        // Update CA1 level (ATN level) without edge-triggering
        ca1Level = busAtnLow;

        // Update Port B input pins to match current bus level
        uint8_t pins = portBPins;

        if (dataLow) pins |=  (1u << IEC_DATA_IN_BIT);
        else         pins &= ~(1u << IEC_DATA_IN_BIT);

        if (clkLow)  pins |=  (1u << IEC_CLK_IN_BIT);
        else         pins &= ~(1u << IEC_CLK_IN_BIT);

        if (atnLow)  pins |=  (1u << IEC_ATN_IN_BIT);
        else         pins &= ~(1u << IEC_ATN_IN_BIT);

        portBPins = pins;

        return;
    }

    const bool prevAtnLow = busAtnLow;
    const bool prevClkLow = busClkLow;

    busAtnLow  = atnLow;
    busClkLow  = clkLow;
    busDataLow = dataLow;

    // Edge Detection
    const bool atnFallingEdge = !prevAtnLow && busAtnLow; // High -> Low (Host asserts ATN)
    const bool atnRisingEdge  = prevAtnLow && !busAtnLow; // Low -> High (Host releases ATN)
    const bool clkRisingEdge  = prevClkLow && !busClkLow; // Low -> High (Host releases CLK)

    // --- 7474 ATN ACK Handshake Logic ---

    if (atnFallingEdge)
    {
        if (!isAtnAckClearAsserted())
        {
            atnAckLatch = true;
            atnAckArmed = true; // Arm for the CLK hold-off check logic (if used)
        }
        else
        {
            // If PB4 is asserting clear, the latch cannot set.
            atnAckLatch = false;
            atnAckArmed = false;
        }
    }

    if (atnRisingEdge)
    {
        atnAckLatch = false;
        atnAckArmed = false;
    }

    if (busAtnLow && clkRisingEdge && atnAckArmed && !isAtnAckClearAsserted())
    {
        atnAckLatch = true;
        atnAckArmed = false;
    }

    if (atnFallingEdge || atnRisingEdge || clkRisingEdge)
    {
        updateIECOutputsFromPortB();
    }

    // --- CA1 Interrupt (ATN Edge) ---
    setCA1Level(busAtnLow);

    // --- Update Port B Input Pins ---
    uint8_t pins = portBPins;

    if (dataLow) pins |=  (1u << IEC_DATA_IN_BIT);
    else         pins &= ~(1u << IEC_DATA_IN_BIT);

    if (clkLow)  pins |=  (1u << IEC_CLK_IN_BIT);
    else         pins &= ~(1u << IEC_CLK_IN_BIT);

    if (atnLow)  pins |=  (1u << IEC_ATN_IN_BIT);
    else         pins &= ~(1u << IEC_ATN_IN_BIT);

    portBPins = pins;

    // --- Serial Shift Register (Fast Serial) ---
    if (clkRisingEdge || (!prevClkLow && busClkLow)) // Rising or Falling
        onClkEdge(prevClkLow && !busClkLow, !prevClkLow && busClkLow);
}

void D1541VIA::clearMechBytePending()
{
    mechBytePending = false;
    clearIFR(IFR_CA1);
}

void D1541VIA::onClkEdge(bool rising, bool falling)
{
    if (viaRole != VIARole::VIA1_IECBus)
        return;

    const uint8_t srMode = (registers.auxControlRegister >> 2) & 0x07;
    const bool srShiftIn = (srMode >= 1 && srMode <= 3);

    if (rising && srShiftIn)
    {
        bool dataLow = ((portBPins & (1u << IEC_DATA_IN_BIT)) != 0);
        int bit = dataLow ? 0 : 1;

        srShiftReg |= static_cast<uint8_t>(bit << srBitCount);
        ++srBitCount;

        if (srBitCount == 8)
        {
            registers.serialShift = srShiftReg;
            srShiftReg = 0;
            srBitCount = 0;
            triggerInterrupt(IFR_SR);
        }
    }
}

void D1541VIA::updateIECOutputsFromPortB()
{
    if (!parentPeripheral || viaRole != VIARole::VIA1_IECBus)
        return;

    auto* drive = static_cast<D1541*>(parentPeripheral);

    const uint8_t ddrB = registers.ddrB;
    const uint8_t orb  = registers.orbIRB;

    // VIA-driven DATA (normal serial output)
    const bool dataOutLow = (ddrB & (1u << IEC_DATA_OUT_BIT)) && ((orb & (1u << IEC_DATA_OUT_BIT)) != 0);

    // VIA-driven CLK
    const bool clkOutLow = (ddrB & (1u << IEC_CLK_OUT_BIT)) && ((orb & (1u << IEC_CLK_OUT_BIT)) != 0);

    const bool atnAckClearActive = isAtnAckClearAsserted();

    // LEVEL-SENSITIVE: if clear is asserted, latch cannot be set.
    if (atnAckClearActive)
    {
        atnAckLatch = false;
        atnAckArmed = false;
    }

    prevAtnAckClear = atnAckClearActive;

    // === 7474 override ===
    // DATA is LOW if either:
    //  - ATN acknowledge latch is set
    //  - VIA is actively driving DATA
    const bool finalDataLow = atnAckLatch || dataOutLow;

    drive->peripheralAssertData(finalDataLow);
    drive->peripheralAssertClk(clkOutLow);
}

DriveVIABase::MechanicsInfo D1541VIA::getMechanicsInfo() const
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

bool D1541VIA::isAtnAckClearAsserted() const
{
    const uint8_t ddrB = registers.ddrB;
    const uint8_t orb  = registers.orbIRB;

    const bool pb4IsOutput = (ddrB & (1u << IEC_ATN_ACK_BIT)) != 0;
    const bool pb4High     = (orb  & (1u << IEC_ATN_ACK_BIT)) != 0;

    if (!pb4IsOutput) return true; // Input (Float High) -> Assert Clear
    return pb4High;                // Output -> Assert Clear if High
}

void D1541VIA::clearIECTransientState()
{
    // clear only transient emu state, NOT registers
    srShiftReg      = 0;
    srBitCount      = 0;
    srShiftInMode   = false;
    iecRxPending    = false;
    iecRxByte       = 0;

    atnAckArmed     = false;
    atnAckLatch     = false;
    prevAtnAckClear = false;

    // Recompute outputs so DATA gets released if latch was holding it low
    updateIECOutputsFromPortB();
}

void D1541VIA::pulseWriteByteReady()
{
    if (viaRole != VIARole::VIA2_Mechanics)
        return;

    triggerInterrupt(IFR_CA1);

    if (parentPeripheral)
    {
        if (auto* drive = dynamic_cast<D1541*>(parentPeripheral))
        {
            if (auto* cpu = drive->getDriveCPU())
                cpu->pulseSO();
        }
    }
}

void D1541VIA::recomputeDiskWriteGate()
{
    if (viaRole != VIARole::VIA2_Mechanics)
        return;

    auto* drive = dynamic_cast<D1541*>(parentPeripheral);
    if (!drive)
        return;

    const uint8_t pcr = registers.peripheralControlRegister;
    const uint8_t ca2Mode = (pcr >> 1) & 0x07;
    const uint8_t cb2Mode = (pcr >> 5) & 0x07;

    const bool portAOutput = (registers.ddrA == 0xFF);

    // Your log shows this is the only phase where DDRA is FF:
    // PCR=$CE DDRA=$FF CA2=7 CB2=6
    const bool pcrWritePhase =
        (pcr == 0xCE) || (ca2Mode == 0b111 && cb2Mode == 0b110);

    const bool gate = portAOutput && pcrWritePhase;

#ifdef Debug
    static bool lastGate = false;
    static uint8_t lastPcr = 0xFF;
    static uint8_t lastDdra = 0xFF;

    // Only print important transitions.
    if (gate != lastGate || registers.ddrA != lastDdra || pcr != lastPcr)
    {
        if (gate || lastGate || registers.ddrA == 0xFF)
        {
            std::cout << "[VIA2:GATE] "
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

void D1541VIA::clearMechLatch()
{
    mechDataLatch   = 0xFF;
    mechBytePending = false;
}

void D1541VIA::onAttachedToPeripheral()
{
    if (viaRole == VIARole::VIA1_IECBus)
        updateIECOutputsFromPortB();

    if (viaRole == VIARole::VIA2_Mechanics)
        recomputeDiskWriteGate();
}

void D1541VIA::onPCRChanged(uint8_t oldValue, uint8_t newValue)
{
    (void)oldValue;
    (void)newValue;

    if (viaRole == VIARole::VIA2_Mechanics)
        recomputeDiskWriteGate();
}

uint8_t D1541VIA::samplePortAInputs()
{
    return portAPins;
}

uint8_t D1541VIA::samplePortBInputs()
{
    return portBPins;
}

void D1541VIA::onPortAOutputChanged(uint8_t oldValue, uint8_t newValue)
{
    (void)oldValue;

    if (viaRole != VIARole::VIA2_Mechanics)
        return;

    if (auto* drive = dynamic_cast<D1541*>(parentPeripheral))
        drive->onVIA2PortAWrite(newValue, registers.ddrA);
}

void D1541VIA::onPortBOutputChanged(uint8_t oldValue, uint8_t newValue)
{
    if (viaRole == VIARole::VIA1_IECBus)
    {
        updateIECOutputsFromPortB();
        return;
    }

    if (viaRole != VIARole::VIA2_Mechanics)
        return;

    auto* drive = dynamic_cast<D1541*>(parentPeripheral);
    if (!drive)
        return;

    const uint8_t ddrB = registers.ddrB;
    const uint8_t phaseMask = 0x03;
    const uint8_t oldPhase = static_cast<uint8_t>(oldValue & phaseMask);
    const uint8_t newPhase = static_cast<uint8_t>(newValue & phaseMask);

    if (oldPhase != newPhase)
        drive->onStepperPhaseChange(oldPhase, newPhase);

    if (ddrB & static_cast<uint8_t>(1u << MECH_SPINDLE_MOTOR))
    {
        const bool enable = (newValue & static_cast<uint8_t>(1u << MECH_SPINDLE_MOTOR)) != 0;

        if (enable)
            drive->startMotor();
        else
            drive->stopMotor();
    }

    if (ddrB & static_cast<uint8_t>(1u << MECH_LED))
        setLed((newValue & static_cast<uint8_t>(1u << MECH_LED)) != 0);

    const uint8_t densityMask = static_cast<uint8_t>((1u << MECH_DENSITY_BIT0) | (1u << MECH_DENSITY_BIT1));

    if (ddrB & densityMask)
    {
        const uint8_t code = static_cast<uint8_t>(((newValue >> MECH_DENSITY_BIT0) & 0x01) |
                (((newValue >> MECH_DENSITY_BIT1) & 0x01) << 1));

        drive->setDensityCode(code);
    }
}

void D1541VIA::onDDRAChanged(uint8_t oldDDR, uint8_t newDDR)
{
    (void)oldDDR;
    (void)newDDR;

    if (viaRole == VIARole::VIA2_Mechanics)
        recomputeDiskWriteGate();
}

void D1541VIA::onDDRBChanged(uint8_t oldDDR, uint8_t newDDR)
{
    (void)oldDDR;

    if (viaRole == VIARole::VIA1_IECBus)
    {
        updateIECOutputsFromPortB();
        return;
    }

    if (viaRole != VIARole::VIA2_Mechanics)
        return;

    auto* drive = dynamic_cast<D1541*>(parentPeripheral);
    if (!drive)
        return;

    const uint8_t orb = registers.orbIRB;

    // Spindle motor
    if (newDDR & static_cast<uint8_t>(1u << MECH_SPINDLE_MOTOR))
    {
        const bool enable = (orb & static_cast<uint8_t>(1u << MECH_SPINDLE_MOTOR)) != 0;

        if (enable)
            drive->startMotor();
        else
            drive->stopMotor();
    }

    // Drive LED
    if (newDDR & static_cast<uint8_t>(1u << MECH_LED))
        setLed((orb & static_cast<uint8_t>(1u << MECH_LED)) != 0);

    // Disk density
    const uint8_t densityMask = static_cast<uint8_t>((1u << MECH_DENSITY_BIT0) | (1u << MECH_DENSITY_BIT1));

    if ((newDDR & densityMask) == densityMask)
    {
        const uint8_t code = static_cast<uint8_t>(((orb >> MECH_DENSITY_BIT0) & 0x01) | (((orb >> MECH_DENSITY_BIT1) & 0x01) << 1));
        drive->setDensityCode(code);
    }
}
