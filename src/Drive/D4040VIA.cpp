#include "D4040VIA.h"

D4040VIA::D4040VIA() :
    ledOn(false),
    syncDetected(false),
    mechDataLatch(0xFF),
    mechBytePending(false)
{

}

D4040VIA::~D4040VIA() = default;


void D4040VIA::reset()
{
    DriveVIA6522::reset();

    ledOn = false;
    syncDetected = false;

    mechDataLatch = 0xFF;
    mechBytePending = false;
}


void D4040VIA::saveState(StateWriter& wrtr) const
{
    saveVIAState(wrtr);

    wrtr.writeBool(ledOn);
    wrtr.writeBool(syncDetected);
    wrtr.writeU8(mechDataLatch);
    wrtr.writeBool(mechBytePending);
}

bool D4040VIA::loadState(StateReader& rdr)
{
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

    return true;
}

uint8_t D4040VIA::readRegister(uint16_t address)
{
    address &= 0x0F;

    uint8_t value = 0xFF;

    // Port A with handshake
    if (address == 0x01)
        return readPortARegister(samplePortAInputs(), true);

    // Port A without handshake
    if (address == 0x0F)
    {
        readCommonPortANoHandshake(address, readPortAPins(samplePortAInputs()), value);
        return value;
    }

    // Port B and direction registers
    if (readCommonPortRegister(address, value))
        return value;

    // Timer 1 / Timer 2
    if (readTimerRegister(address, value))
        return value;

    // Shift register
    if (readCommonShiftRegister(address, value))
        return value;

    // ACR / PCR
    if (readControlRegister(address, value))
        return value;

    // IFR / IER
    if (readInterruptRegister(address, value))
        return value;

    return 0xFF;
}


void D4040VIA::writeRegister(uint16_t address, uint8_t value)
{
    address &= 0x0F;

    // Port A/B and direction registers
    if (writeCommonPortRegister(address, value))
        return;

    // Port A without handshake
    if (address == 0x0F)
    {
        const uint8_t oldValue = registers.oraIRA;

        writePortALatch(value, true);

        onPortAOutputChanged(oldValue, value);
        return;
    }

    // Timer 1 / Timer 2
    if (writeTimerRegister(address, value))
        return;

    // Shift register
    if (writeCommonShiftRegister(address, value))
        return;

    // ACR / PCR
    if (writeControlRegister(address, value))
        return;

    // IFR / IER
    if (writeInterruptRegister(address, value))
        return;
}


uint8_t D4040VIA::peek(uint16_t address) const
{
    switch (address & 0x0F)
    {
        case 0x00:
            return readPortBPins(portBPins);

        case 0x01:
        case 0x0F:
            return readPortAPins(portAPins);

        case 0x02:
            return registers.ddrB;

        case 0x03:
            return registers.ddrA;

        case 0x04:
            return registers.timer1CounterLowByte;

        case 0x05:
            return registers.timer1CounterHighByte;

        case 0x06:
            return registers.timer1LowLatch;

        case 0x07:
            return registers.timer1HighLatch;

        case 0x08:
            return registers.timer2CounterLowByte;

        case 0x09:
            return registers.timer2CounterHighByte;

        case 0x0A:
            return registers.serialShift;

        case 0x0B:
            return registers.auxControlRegister;

        case 0x0C:
            return registers.peripheralControlRegister;

        case 0x0D:
            return registers.interruptFlag;

        case 0x0E:
            return registers.interruptEnable;
    }

    return 0xFF;
}

D4040MotorSignals D4040VIA::getMotorSignals() const
{
    D4040MotorSignals result;

    // Resolved output state.
    const uint8_t pb = static_cast<uint8_t>(registers.orbIRB | ~registers.ddrB);

    result.stepper1 = pb & 0x03;
    result.stepper0 = (pb >> 2) & 0x03;

    // Motor controls are active-low.
    result.motor1On = (pb & 0x10) == 0;
    result.motor0On = (pb & 0x20) == 0;

    return result;
}

uint8_t D4040VIA::samplePortAInputs()
{
    return 0xFF;
}

uint8_t D4040VIA::samplePortBInputs()
{
    return 0xFF;
}

void D4040VIA::onPortAOutputChanged(uint8_t oldValue, uint8_t newValue)
{

}

void D4040VIA::onPortBOutputChanged(uint8_t oldValue, uint8_t newValue)
{

}

void D4040VIA::onDDRAChanged(uint8_t oldDDR, uint8_t newDDR)
{

}

void D4040VIA::onDDRBChanged(uint8_t oldDDR, uint8_t newDDR)
{

}

void D4040VIA::onPCRChanged(uint8_t oldValue, uint8_t newValue)
{

}

void D4040VIA::onAttachedToPeripheral()
{

}

DriveVIA6522::MechanicsInfo D4040VIA::getMechanicsInfo() const
{
    return {};
}
