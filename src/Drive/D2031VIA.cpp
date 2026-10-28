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

D2031VIA::~D2031VIA()
{

}

void D2031VIA::saveState(StateWriter& wrtr) const
{

}

bool D2031VIA::loadState(StateReader& rdr)
{

}

void D2031VIA::reset()
{
    DriveVIA6522::reset();
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

}

void D2031VIA::clearIEEE488TransientState()
{

}

void D2031VIA::onAttachedToPeripheral()
{
    if (viaRole == VIARole::VIA1_IEEE488Bus)
        updateIEEE488Outputs();

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
    uint8_t pins = portAPins;

    if (viaRole == VIARole::VIA2_Mechanics)
        return mechDataLatch;

    if (viaRole != VIARole::VIA1_IEEE488Bus)
        return pins;

    return pins;
}

uint8_t D2031VIA::samplePortBInputs()
{
    uint8_t pins = portBPins;
    const uint8_t ddrB = registers.ddrB;

    if (viaRole == VIARole::VIA1_IEEE488Bus)
    {
        if (auto* drive = dynamic_cast<D2031*>(parentPeripheral))
        {
            auto setPin = [&](uint8_t bit, bool high)
            {
                const uint8_t mask =
                    static_cast<uint8_t>(1u << bit);

                if (high)
                    pins |= mask;
                else
                    pins &= static_cast<uint8_t>(~mask);
            };

            setPin(IEEE488_NRFD_BIT, drive->getIEEE488Signal(IEEE488Bus::Signal::NRFD));
            setPin(IEEE488_NDAC_BIT, drive->getIEEE488Signal(IEEE488Bus::Signal::NDAC));
            setPin(IEEE488_EOI_BIT, drive->getIEEE488Signal(IEEE488Bus::Signal::EOI));
            setPin(IEEE488_DAV_BIT, drive->getIEEE488Signal(IEEE488Bus::Signal::DAV));

            // PB7 receives inverted ATN.
            setPin(IEEE488_ATN_BIT, !drive->getIEEE488Signal(IEEE488Bus::Signal::ATN));
        }
    }
    else if (viaRole == VIARole::VIA2_Mechanics)
    {
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
    }

    return pins;
}

void D2031VIA::onPortAOutputChanged(uint8_t oldValue, uint8_t newValue)
{

}

void D2031VIA::onPortBOutputChanged(uint8_t oldValue, uint8_t newValue)
{

}

void D2031VIA::onDDRAChanged(uint8_t oldDDR, uint8_t newDDR)
{

}

void D2031VIA::onDDRBChanged(uint8_t oldDDR, uint8_t newDDR)
{

}

void D2031VIA::onShiftRegisterWritten()
{

}

uint8_t D2031VIA::sampleShiftRegisterRead()
{
    if (viaRole == VIARole::VIA2_Mechanics)
        return mechDataLatch;

    return registers.serialShift;
}

void D2031VIA::updateIEEE488Outputs()
{

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
