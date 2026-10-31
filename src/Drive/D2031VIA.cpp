// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Drive/D2031.h"
#include "Drive/D2031VIA.h"

D2031VIA::D2031VIA() :
    ledOn(false),
    syncDetected(false),
    mechDataLatch(0xFF),
    mechBytePending(false)
{

}

D2031VIA::~D2031VIA() = default;

void D2031VIA::saveState(StateWriter& wrtr) const
{
    wrtr.writeU32(1);

    saveVIAState(wrtr);

    wrtr.writeBool(ledOn);
    wrtr.writeBool(syncDetected);
    wrtr.writeU8(mechDataLatch);
    wrtr.writeBool(mechBytePending);
}

bool D2031VIA::loadState(StateReader& rdr)
{
    uint32_t version = 0;

    if (!rdr.readU32(version))
        return false;

    if (version != 1)
        return false;

    if (!loadVIAState(rdr))
        return false;

    if (!rdr.readBool(ledOn))
        return false;

    if (!rdr.readBool(syncDetected))
        return false;

    if (!rdr.readU8(mechDataLatch))
        return false;

    if (!rdr.readBool(mechBytePending))
        return false;

    refreshMasterBit();

    return true;
}

void D2031VIA::reset()
{
    DriveVIA6522::reset();

    ledOn           = false;
    syncDetected    = false;
    mechDataLatch   = 0;
    mechBytePending = false;
}

uint8_t D2031VIA::readRegister(uint16_t address)
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
            // Sample hardware inputs before consuming disk data.
            const uint8_t inputPins = samplePortAInputs();

            // Preserve the existing disk-byte consumption behavior.
            if (viaRole == VIARole::VIA2_Mechanics && mechBytePending)
            {
                if (auto* drive = dynamic_cast<D2031*>(parentPeripheral))
                    drive->onVIA2PortARead(mechDataLatch);

                mechBytePending = false;
            }

            // Shared DDR masking and CA1/CA2 acknowledgement.
            return readPortARegister(inputPins, true);
        }

        case 0x0F: // ORA/IRA no handshake
        {
            const uint8_t sampledValue = readPortARegister(samplePortAInputs(), false);
            uint8_t result = 0xFF;

            if (readCommonPortANoHandshake(address, sampledValue, result))
                return result;

            return sampledValue;
        }

        default:
            return 0xFF;
    }
}

void D2031VIA::writeRegister(uint16_t address, uint8_t value)
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

    if (writeCommonShiftRegister(address, value))
        return;

    switch (address)
    {
        case 0x0F: // ORA/IRA no handshake
        {
            const uint8_t oldValue = registers.oraIRA;

            writePortALatch(value);
            onPortAOutputChanged(oldValue, value);
            break;
        }

        default:
            break;
    }
}

void D2031VIA::diskByteFromMedia(uint8_t byte, bool inSync)
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

    mechDataLatch = byte;
    mechBytePending = true;

    triggerInterrupt(IFR_CA1);

    if (parentPeripheral)
    {
        if (auto* drive = dynamic_cast<D2031*>(parentPeripheral))
        {
            if (auto* cpu = drive->getDriveCPU())
                cpu->pulseSO();
        }
    }
}

void D2031VIA::clearIEEE488TransientState()
{
    // clear only transient emu state, NOT registers
    srShiftReg      = 0;
    srBitCount      = 0;

    updateIEEE488OutputsFromPortB();
}

void D2031VIA::clearMechBytePending()
{
    mechBytePending = false;
    clearIFR(IFR_CA1);
}

void D2031VIA::synchronizeIEEE488Outputs()
{
    if (viaRole != VIARole::VIA1_IEEE488Bus)
        return;

    updateIEEE488OutputsFromPortB();
    onPortAOutputChanged(registers.oraIRA, registers.oraIRA);
    updateIEEE488DeviceAddress();
}

void D2031VIA::resetShift()
{
    srShiftReg = 0;
    srBitCount = 0;
}

void D2031VIA::pulseWriteByteReady()
{
    if (viaRole != VIARole::VIA2_Mechanics)
        return;

    // Signal the VIA CA1 interrupt.
    triggerInterrupt(IFR_CA1);

    // Notify the drive CPU that a disk byte is ready.
    if (parentPeripheral)
    {
        if (auto* drive = dynamic_cast<D2031*>(parentPeripheral))
        {
            if (auto* cpu = drive->getDriveCPU())
                cpu->pulseSO();
        }
    }
}

void D2031VIA::setIEEE488Inputs(const IEEE488Bus::BusState& oldState, const IEEE488Bus::BusState& newState)
{
    if (viaRole != VIARole::VIA1_IEEE488Bus)
        return;

    // VIA1 Port A:
    // IEEE-488 parallel data lines.
    portAPins = newState.dio;

    // VIA1 CA1:
    // The Commodore 2031 connects inverted ATN to CA1.
    //
    // IEEE-488 ATN:
    //   true  = high / released
    //   false = low / asserted
    //
    // CA1 receives the opposite polarity.
    if (oldState.atn != newState.atn)
    {
        setCA1Level(!newState.atn);

        // ATN also affects the external handshake circuit.
        updateIEEE488OutputsFromPortB();
    }
}

void D2031VIA::onAttachedToPeripheral()
{
    if (viaRole == VIARole::VIA1_IEEE488Bus)
        updateIEEE488OutputsFromPortB();

    if (viaRole == VIARole::VIA2_Mechanics)
        recomputeDiskWriteGate();
}

void D2031VIA::onPCRChanged(uint8_t oldValue, uint8_t newValue)
{
    (void)oldValue;
    (void)newValue;

    if (viaRole == VIARole::VIA2_Mechanics)
        recomputeDiskWriteGate();
}

uint8_t D2031VIA::samplePortAInputs()
{
    if (viaRole == VIARole::VIA2_Mechanics)
        return mechDataLatch;

    if (viaRole == VIARole::VIA1_IEEE488Bus)
    {
        if (auto* drive = dynamic_cast<D2031*>(parentPeripheral))
            return drive->getIEEE488BusDIO();
    }

    return portAPins;
}

uint8_t D2031VIA::samplePortBInputs()
{
    if (viaRole == VIARole::VIA1_IEEE488Bus)
    {
        auto* drive = dynamic_cast<D2031*>(parentPeripheral);

        if (!drive)
            return 0x00;

        // Construct the externally supplied VIA1 inputs.
        // The VIA base class applies DDR masking afterward.
        uint8_t pins = 0x00;

        auto setPin = [&](uint8_t bit, bool high)
        {
            if (high)
                pins |= static_cast<uint8_t>(1u << bit);
        };

        // The ROM samples PB0/PB1 to initialize its IEEE-488 address.
        // Compatibility behavior validated for device numbers 8-11.
        const uint8_t addressBits = static_cast<uint8_t>(drive->getDeviceNumber() - 8) & 0x03;

        // PB0 supplies the low-order address bit when configured as input.
        setPin(IEEE488_ATNA_BIT, (addressBits & 0x01) != 0);

        // PB1 supplies the second address bit during ROM initialization.
        // Otherwise PB1 remains the IEEE-488 NRFD handshake input.
        if (registers.ddrB == 0x30)
            setPin(IEEE488_NRFD_BIT, (addressBits & 0x02) != 0);
        else
            setPin(IEEE488_NRFD_BIT, drive->getIEEE488Signal(IEEE488Bus::Signal::NRFD));

        setPin(IEEE488_NDAC_BIT, drive->getIEEE488Signal(IEEE488Bus::Signal::NDAC));
        setPin(IEEE488_EOI_BIT, drive->getIEEE488Signal(IEEE488Bus::Signal::EOI));
        setPin(IEEE488_DAV_BIT, drive->getIEEE488Signal(IEEE488Bus::Signal::DAV));

        // PB7 receives inverted ATN.
        setPin(IEEE488_ATN_BIT, !drive->getIEEE488Signal(IEEE488Bus::Signal::ATN));

        return pins;
    }

    if (viaRole == VIARole::VIA2_Mechanics)
    {
        uint8_t pins = portBPins;
        const uint8_t ddrB = registers.ddrB;

        if (auto* drive = dynamic_cast<D2031*>(parentPeripheral))
        {
            if ((ddrB & (1u << MECH_WRITE_PROTECT)) == 0)
            {
                if (drive->isWriteProtected())
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

        return pins;
    }

    return portBPins;
}

void D2031VIA::onPortAOutputChanged(uint8_t oldValue, uint8_t newValue)
{
    (void)oldValue;
    (void)newValue;

    auto* drive = dynamic_cast<D2031*>(parentPeripheral);
    if (!drive)
        return;

    if (viaRole == VIARole::VIA1_IEEE488Bus)
    {
        const uint8_t output = static_cast<uint8_t>((registers.oraIRA & registers.ddrA) |~registers.ddrA);

        (void)drive->setIEEE488BusDIO(output);
        return;
    }

    if (viaRole == VIARole::VIA2_Mechanics)
        drive->onVIA2PortAWrite(registers.oraIRA, registers.ddrA );
}

void D2031VIA::onPortBOutputChanged(uint8_t oldValue, uint8_t newValue)
{
    if (viaRole == VIARole::VIA1_IEEE488Bus)
    {
        // Update the IEEE-488 handshake outputs.
        updateIEEE488OutputsFromPortB();

        // Update CA2 from the configured device-address circuitry.
        updateIEEE488DeviceAddress();

        return;
    }

    // VIA2: Disk Mechanism
    if (viaRole != VIARole::VIA2_Mechanics)
        return;

    auto* drive = dynamic_cast<D2031*>(parentPeripheral);
    if (!drive)
        return;

    const uint8_t ddrB = registers.ddrB;

    // Stepper motor phases: PB0-PB1
    const uint8_t phaseMask = 0x03;

    const uint8_t oldPhase = static_cast<uint8_t>(oldValue & phaseMask);
    const uint8_t newPhase = static_cast<uint8_t>(newValue & phaseMask);

    if (oldPhase != newPhase && (ddrB & phaseMask) == phaseMask)
        drive->onStepperPhaseChange(oldPhase, newPhase);

    // Spindle motor: PB2
    if (ddrB & (1u << MECH_SPINDLE_MOTOR))
    {
        const bool enabled = (newValue & (1u << MECH_SPINDLE_MOTOR)) != 0;

        if (enabled)
            drive->startMotor();
        else
            drive->stopMotor();
    }

    // Activity LED: PB3
    if (ddrB & (1u << MECH_LED))
    {
        const bool enabled = (newValue & (1u << MECH_LED)) != 0;
        setLed(enabled);
    }

    // Density selection: PB5-PB6
    const uint8_t densityMask = static_cast<uint8_t>((1u << MECH_DENSITY_BIT0) |(1u << MECH_DENSITY_BIT1));

    if ((ddrB & densityMask) == densityMask)
    {
        const uint8_t code = static_cast<uint8_t>(((newValue >> MECH_DENSITY_BIT0) & 0x01) | (((newValue >> MECH_DENSITY_BIT1) & 0x01) << 1));

        drive->setDensityCode(code);
    }
}

void D2031VIA::onDDRAChanged(uint8_t oldDDR, uint8_t newDDR)
{
    (void)oldDDR;
    (void)newDDR;

    if (viaRole == VIARole::VIA1_IEEE488Bus)
    {
        onPortAOutputChanged(registers.oraIRA, registers.oraIRA);
        return;
    }

    if (viaRole == VIARole::VIA2_Mechanics)
        recomputeDiskWriteGate();
}

void D2031VIA::onDDRBChanged(uint8_t oldDDR, uint8_t newDDR)
{
    if (viaRole == VIARole::VIA1_IEEE488Bus)
    {
        updateIEEE488OutputsFromPortB();
        updateIEEE488DeviceAddress();
        return;
    }

    if (viaRole != VIARole::VIA2_Mechanics)
        return;

    auto* drive = dynamic_cast<D2031*>(parentPeripheral);
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

void D2031VIA::onShiftRegisterWritten()
{
    resetShift();
}

uint8_t D2031VIA::sampleShiftRegisterRead()
{
    return registers.serialShift;
}

void D2031VIA::updateIEEE488OutputsFromPortB()
{
    if (viaRole != VIARole::VIA1_IEEE488Bus)
        return;

    auto* drive = dynamic_cast<D2031*>(parentPeripheral);
    if (!drive)
        return;

    const uint8_t orb  = registers.orbIRB;
    const uint8_t ddrB = registers.ddrB;

    // Resolve the VIA output pin levels.
    // Input-configured pins default high.
    const uint8_t pb = static_cast<uint8_t>((orb & ddrB) | ~ddrB);

    const bool atna = (pb >> IEEE488_ATNA_BIT) & 1;
    bool nrfd = (pb >> IEEE488_NRFD_BIT) & 1;
    bool ndac = (pb >> IEEE488_NDAC_BIT) & 1;

    const bool eoi = (pb >> IEEE488_EOI_BIT) & 1;
    const bool dav = (pb >> IEEE488_DAV_BIT) & 1;

    // IEEE-488 bus uses true = high/released.
    const bool atnHigh = drive->getIEEE488Signal(IEEE488Bus::Signal::ATN);

    // Hardware ATN acknowledge interaction.
    if ((!atnHigh) ^ atna)
    {
        nrfd = false;
        ndac = false;
    }

    (void)drive->setIEEE488Signal(IEEE488Bus::Signal::NRFD, nrfd);
    (void)drive->setIEEE488Signal(IEEE488Bus::Signal::NDAC, ndac);
    (void)drive->setIEEE488Signal(IEEE488Bus::Signal::EOI, eoi);
    (void)drive->setIEEE488Signal(IEEE488Bus::Signal::DAV, dav);
}

void D2031VIA::recomputeDiskWriteGate()
{
    if (viaRole != VIARole::VIA2_Mechanics)
        return;

    auto* drive = dynamic_cast<D2031*>(parentPeripheral);
    if (!drive)
        return;

    const uint8_t pcr = registers.peripheralControlRegister;
    const uint8_t ca2Mode = (pcr >> 1) & 0x07;
    const uint8_t cb2Mode = (pcr >> 5) & 0x07;

    const bool portAOutput = (registers.ddrA == 0xFF);
    const bool pcrWritePhase = (pcr == 0xCE) || (ca2Mode == 0b111 && cb2Mode == 0b110);

    const bool gate = portAOutput && pcrWritePhase;
    drive->setDiskWriteGate(gate);
}

DriveVIABase::MechanicsInfo D2031VIA::getMechanicsInfo() const
{
    MechanicsInfo m{};
    m.valid = false;

    if (viaRole != VIARole::VIA2_Mechanics)
        return m;

    uint8_t orb  = registers.orbIRB;
    uint8_t ddrB = registers.ddrB;

    m.valid = true;

    if (ddrB & (1u << MECH_SPINDLE_MOTOR))
        m.motorOn = (orb & (1u << MECH_SPINDLE_MOTOR)) != 0;
    else
        m.motorOn = false;

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


void D2031VIA::updateIEEE488DeviceAddress()
{
    if (viaRole != VIARole::VIA1_IEEE488Bus)
        return;

    auto* drive = dynamic_cast<D2031*>(parentPeripheral);
    if (!drive)
        return;

    // VIA1 output latch values used by the address-select circuit.
    const uint8_t orb = registers.orbIRB;

    const bool atna = (orb & 0x01) != 0;
    const bool nrfd = (orb & 0x02) != 0;

    bool ca2 = true;

    // Commodore 2031 device address diode logic.
    switch (drive->getDeviceNumber())
    {
        case 8:
            ca2 = atna && nrfd;
            break;

        case 9:
            ca2 = nrfd;
            break;

        case 10:
            ca2 = atna;
            break;

        case 11:
            ca2 = true;
            break;

        default:
            ca2 = true;
            break;
    }

    setCA2Level(ca2);
}
