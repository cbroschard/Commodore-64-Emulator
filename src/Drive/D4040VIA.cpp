#include "D4040VIA.h"

D4040VIA::D4040VIA()
{

}

D4040VIA::~D4040VIA() = default;

void D4040VIA::reset()
{
    DriveVIA6522::reset();
}

void D4040VIA::saveState(StateWriter& wrtr) const
{

}

bool D4040VIA::loadState(StateReader& rdr)
{
    return true;
}

uint8_t D4040VIA::readRegister(uint16_t address)
{
    return 0xFF;
}

void D4040VIA::writeRegister(uint16_t address, uint8_t value)
{

}

uint8_t D4040VIA::peek(uint16_t address) const
{
    return 0xFF;
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
