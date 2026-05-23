// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Drive/DriveVIA6522.h"

DriveVIA6522::DriveVIA6522() :
    viaRole(VIARole::Unknown),
    timer1Counter(0),
    timer1Latch(0),
    timer1Running(false),
    timer1JustLoaded(false),
    timer1ReloadPending(false),
    timer1InhibitIRQ(false),
    timer1PB7Level(true),
    timer2Counter(0),
    timer2Latch(0),
    timer2Running(false),
    timer2JustLoaded(false),
    timer2InhibitIRQ(false),
    timer2LowLatchByte(0x00)
{

}

DriveVIA6522::~DriveVIA6522() = default;

void DriveVIA6522::attachPeripheralInstance(Peripheral* parentPeripheral, VIARole role)
{
    this->parentPeripheral = parentPeripheral;
    this->viaRole = role;
    onAttachedToPeripheral();
}

void DriveVIA6522::saveVIAState(StateWriter& wrtr) const
{
    wrtr.writeU8(static_cast<uint8_t>(viaRole));
    wrtr.writeU8(portBPins);
    wrtr.writeU8(portAPins);

    wrtr.writeU8(registers.orbIRB);
    wrtr.writeU8(registers.oraIRA);
    wrtr.writeU8(registers.ddrA);
    wrtr.writeU8(registers.ddrB);
    wrtr.writeU8(registers.timer1CounterLowByte);
    wrtr.writeU8(registers.timer1CounterHighByte);
    wrtr.writeU8(registers.timer1LowLatch);
    wrtr.writeU8(registers.timer1HighLatch);
    wrtr.writeU8(registers.timer2CounterLowByte);
    wrtr.writeU8(registers.timer2CounterHighByte);
    wrtr.writeU8(registers.serialShift);
    wrtr.writeU8(registers.auxControlRegister);
    wrtr.writeU8(registers.peripheralControlRegister);
    wrtr.writeU8(registers.interruptFlag);
    wrtr.writeU8(registers.interruptEnable);
    wrtr.writeU8(registers.oraIRANoHandshake);

    wrtr.writeU16(timer1Counter);
    wrtr.writeU16(timer1Latch);
    wrtr.writeBool(timer1Running);
    wrtr.writeBool(timer1JustLoaded);
    wrtr.writeBool(timer1ReloadPending);
    wrtr.writeBool(timer1InhibitIRQ);
    wrtr.writeBool(timer1PB7Level);

    wrtr.writeU16(timer2Counter);
    wrtr.writeU16(timer2Latch);
    wrtr.writeBool(timer2Running);
    wrtr.writeBool(timer2JustLoaded);
    wrtr.writeBool(timer2InhibitIRQ);
    wrtr.writeU8(timer2LowLatchByte);
}

bool DriveVIA6522::loadVIAState(StateReader& rdr)
{
    uint8_t roleU8 = 0;
    if (!rdr.readU8(roleU8)) return false;
    viaRole = static_cast<VIARole>(roleU8);

    if (!rdr.readU8(portBPins)) return false;
    if (!rdr.readU8(portAPins)) return false;

    if (!rdr.readU8(registers.orbIRB)) return false;
    if (!rdr.readU8(registers.oraIRA)) return false;
    if (!rdr.readU8(registers.ddrA)) return false;
    if (!rdr.readU8(registers.ddrB)) return false;
    if (!rdr.readU8(registers.timer1CounterLowByte)) return false;
    if (!rdr.readU8(registers.timer1CounterHighByte)) return false;
    if (!rdr.readU8(registers.timer1LowLatch)) return false;
    if (!rdr.readU8(registers.timer1HighLatch)) return false;
    if (!rdr.readU8(registers.timer2CounterLowByte)) return false;
    if (!rdr.readU8(registers.timer2CounterHighByte)) return false;
    if (!rdr.readU8(registers.serialShift)) return false;
    if (!rdr.readU8(registers.auxControlRegister)) return false;
    if (!rdr.readU8(registers.peripheralControlRegister)) return false;
    if (!rdr.readU8(registers.interruptFlag)) return false;
    if (!rdr.readU8(registers.interruptEnable)) return false;
    if (!rdr.readU8(registers.oraIRANoHandshake)) return false;

    if (!rdr.readU16(timer1Counter)) return false;
    if (!rdr.readU16(timer1Latch)) return false;
    if (!rdr.readBool(timer1Running)) return false;
    if (!rdr.readBool(timer1JustLoaded)) return false;
    if (!rdr.readBool(timer1ReloadPending)) return false;
    if (!rdr.readBool(timer1InhibitIRQ)) return false;
    if (!rdr.readBool(timer1PB7Level)) return false;

    if (!rdr.readU16(timer2Counter)) return false;
    if (!rdr.readU16(timer2Latch)) return false;
    if (!rdr.readBool(timer2Running)) return false;
    if (!rdr.readBool(timer2JustLoaded)) return false;
    if (!rdr.readBool(timer2InhibitIRQ)) return false;
    if (!rdr.readU8(timer2LowLatchByte)) return false;

    refreshMasterBit();
    return true;
}

void DriveVIA6522::reset()
{
    // Pin defaults
    portAPins                           = 0xFF;
    portBPins                           = 0xFF;

    // Initialize registers
    registers.orbIRB                    = 0x00;
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

    // Timer defaults
    timer1Counter                       = 0;
    timer1Latch                         = 0;
    timer1Running                       = false;
    timer1JustLoaded                    = false;
    timer1ReloadPending                 = false;
    timer1InhibitIRQ                    = false;
    timer1PB7Level                      = true;

    timer2Counter                       = 0;
    timer2Latch                         = 0;
    timer2Running                       = false;
    timer2JustLoaded                    = false;
    timer2InhibitIRQ                    = false;
    timer2LowLatchByte                  = 0x00;
}

void DriveVIA6522::tick(uint32_t cycles)
{
    while (cycles-- > 0)
    {
        timer1Tick();
        timer2Tick();
    }

}

bool DriveVIA6522::checkIRQActive() const
{
    uint8_t active = registers.interruptEnable & registers.interruptFlag & 0x7F;
    return active != 0;
}

void DriveVIA6522::triggerInterrupt(uint8_t sourceMask)
{
    registers.interruptFlag |= sourceMask;
    refreshMasterBit();
}

void DriveVIA6522::clearIFR(uint8_t sourceMask)
{
    registers.interruptFlag &= static_cast<uint8_t>(~sourceMask);
    refreshMasterBit();
}

void DriveVIA6522::refreshMasterBit()
{
    const uint8_t pendingEnabled = registers.interruptFlag & registers.interruptEnable & 0x7F;

    if (pendingEnabled)
        registers.interruptFlag |= IFR_IRQ;
    else
        registers.interruptFlag &= static_cast<uint8_t>(~IFR_IRQ);
}

bool DriveVIA6522::readTimerRegister(uint16_t address, uint8_t& value)
{
    switch (address & 0x0F)
    {
        case 0x04:
            clearIFR(IFR_TIMER1);
            value = static_cast<uint8_t>(timer1Counter & 0xFF);
            return true;

        case 0x05:
            value = static_cast<uint8_t>((timer1Counter >> 8) & 0xFF);
            return true;

        case 0x06:
            value = registers.timer1LowLatch;
            return true;

        case 0x07:
            value = registers.timer1HighLatch;
            return true;

        case 0x08:
            clearIFR(IFR_TIMER2);
            value = static_cast<uint8_t>(timer2Counter & 0xFF);
            return true;

        case 0x09:
            value = static_cast<uint8_t>((timer2Counter >> 8) & 0xFF);
            return true;

        default:
            return false;
    }
}

bool DriveVIA6522::writeTimerRegister(uint16_t address, uint8_t value)
{
    switch (address & 0x0F)
    {
        case 0x04: // T1C-L
        {
            registers.timer1LowLatch = value;
            timer1Latch = static_cast<uint16_t>(
                (timer1Latch & 0xFF00) | value
            );
            return true;
        }

        case 0x05: // T1C-H, load/start
        {
            registers.timer1HighLatch = value;

            timer1Latch =
                static_cast<uint16_t>(
                    (static_cast<uint16_t>(registers.timer1HighLatch) << 8) |
                     static_cast<uint16_t>(registers.timer1LowLatch)
                );

            timer1Counter = timer1Latch;
            timer1Running = true;

            timer1JustLoaded = true;
            timer1ReloadPending = false;
            timer1InhibitIRQ = false;

            clearIFR(IFR_TIMER1);
            syncTimer1Registers();
            return true;
        }

        case 0x06: // T1 latch low
        {
            registers.timer1LowLatch = value;
            timer1Latch = static_cast<uint16_t>(
                (timer1Latch & 0xFF00) | value
            );
            return true;
        }

        case 0x07: // T1 latch high
        {
            registers.timer1HighLatch = value;

            timer1Latch =
                static_cast<uint16_t>(
                    (static_cast<uint16_t>(registers.timer1HighLatch) << 8) |
                     static_cast<uint16_t>(registers.timer1LowLatch)
                );

            clearIFR(IFR_TIMER1);
            return true;
        }

        case 0x08: // T2C-L
        {
            timer2LowLatchByte = value;
            return true;
        }

        case 0x09: // T2C-H, load/start
        {
            registers.timer2CounterHighByte = value;

            timer2Counter =
                static_cast<uint16_t>(
                    (static_cast<uint16_t>(registers.timer2CounterHighByte) << 8) |
                     static_cast<uint16_t>(timer2LowLatchByte)
                );

            timer2Latch = timer2Counter;
            timer2Running = true;

            timer2JustLoaded = true;
            timer2InhibitIRQ = false;

            clearIFR(IFR_TIMER2);
            syncTimer2Registers();
            return true;
        }

        default:
            return false;
    }
}

bool DriveVIA6522::readInterruptRegister(uint16_t address, uint8_t& value)
{
    switch (address & 0x0F)
    {
        case 0x0D: // IFR
        {
            refreshMasterBit();
            value = registers.interruptFlag;
            return true;
        }

        case 0x0E: // IER
        {
            value = registers.interruptEnable;
            return true;
        }

        default:
            return false;
    }
}

bool DriveVIA6522::writeInterruptRegister(uint16_t address, uint8_t value)
{
    switch (address & 0x0F)
    {
        case 0x0D: // IFR
        {
            // Writing 1s clears matching IFR bits. Bit 7 is not directly cleared here.
            const uint8_t mask = static_cast<uint8_t>(value & 0x7F);
            clearIFR(mask);
            return true;
        }

        case 0x0E: // IER
        {
            const bool set = (value & 0x80) != 0;
            const uint8_t mask = static_cast<uint8_t>(value & 0x7F);

            if (set)
            {
                registers.interruptEnable =
                    static_cast<uint8_t>(registers.interruptEnable | mask);
            }
            else
            {
                registers.interruptEnable =
                    static_cast<uint8_t>(registers.interruptEnable & static_cast<uint8_t>(~mask));
            }

            refreshMasterBit();
            return true;
        }

        default:
            return false;
    }
}

bool DriveVIA6522::readControlRegister(uint16_t address, uint8_t& value)
{
    switch (address & 0x0F)
    {
        case 0x0B: // ACR
            value = registers.auxControlRegister;
            return true;

        default:
            return false;
    }
}

bool DriveVIA6522::writeControlRegister(uint16_t address, uint8_t value)
{
    switch (address & 0x0F)
    {
        case 0x0B: // ACR
            registers.auxControlRegister = value;
            return true;

        default:
            return false;
    }
}

void DriveVIA6522::timer1Tick()
{
    if (!timer1Running)
        return;

    if (timer1ReloadPending)
    {
        timer1Counter = timer1Latch;
        timer1ReloadPending = false;
        timer1JustLoaded = true;
        syncTimer1Registers();
    }

    if (timer1JustLoaded)
    {
        timer1JustLoaded = false;
        return;
    }

    timer1Counter = static_cast<uint16_t>(timer1Counter - 1);
    syncTimer1Registers();

    if (timer1Counter == 0x0000 && !timer1InhibitIRQ)
    {
        triggerInterrupt(IFR_TIMER1);

        const bool freeRun =
            (registers.auxControlRegister & ACR_T1_CONTINUOUS) != 0;

        if (freeRun)
        {
            timer1ReloadPending = true;
        }
        else
        {
            timer1InhibitIRQ = true;
        }
    }
}

void DriveVIA6522::timer2Tick()
{
    if (!timer2Running)
        return;

    const bool pulseCountMode =
        (registers.auxControlRegister & ACR_T2_PULSE_COUNT) != 0;

    if (pulseCountMode)
        return;

    if (timer2JustLoaded)
    {
        timer2JustLoaded = false;
        return;
    }

    timer2Counter = static_cast<uint16_t>(timer2Counter - 1);
    syncTimer2Registers();

    if (timer2Counter == 0x0000 && !timer2InhibitIRQ)
    {
        triggerInterrupt(IFR_TIMER2);
        timer2InhibitIRQ = true;
    }
}

void DriveVIA6522::syncTimer1Registers()
{
    registers.timer1CounterLowByte =
        static_cast<uint8_t>(timer1Counter & 0xFF);

    registers.timer1CounterHighByte =
        static_cast<uint8_t>((timer1Counter >> 8) & 0xFF);
}

void DriveVIA6522::syncTimer2Registers()
{
    registers.timer2CounterLowByte =
        static_cast<uint8_t>(timer2Counter & 0xFF);

    registers.timer2CounterHighByte =
        static_cast<uint8_t>((timer2Counter >> 8) & 0xFF);
}
