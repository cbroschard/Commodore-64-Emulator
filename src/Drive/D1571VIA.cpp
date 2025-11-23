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
    viaRole(VIARole::Unknown),
    ledOn(false),
    syncDetectedLow(false),
    t1Counter(0),
    t1Latch(0),
    t1Running(false),
    t2Counter(0),
    t2Latch(0),
    t2Running(false)
{

}

D1571VIA::~D1571VIA() = default;

void D1571VIA::attachPeripheralInstance(Peripheral* parentPeripheral, VIARole viaRole)
{
    this->parentPeripheral = parentPeripheral;
    this->viaRole = viaRole;
}

void D1571VIA::reset()
{
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
    ledOn           = false;
    syncDetectedLow = false;
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
            uint8_t value = registers.orbIRB; // read latch
            uint8_t ddrB = registers.ddrB; // data direction
            if (viaRole == VIARole::VIA1_IECBus)
            {
                if (auto* drive = dynamic_cast<D1571*>(parentPeripheral))
                {
                    // Data In
                    if ((ddrB & (1u << IEC_DATA_IN_BIT)) == 0)
                    {
                        bool low = drive->getDataLineLow();
                        if (low) value &= static_cast<uint8_t>(~(1u << IEC_DATA_IN_BIT));
                        else     value |= static_cast<uint8_t>(1u << IEC_DATA_IN_BIT);
                    }
                    // Clk In
                    if ((ddrB & (1u << IEC_CLK_IN_BIT)) == 0)
                    {
                        bool low = drive->getClkLineLow();
                        if (low) value &= static_cast<uint8_t>(~(1u << IEC_CLK_IN_BIT));
                        else     value |= static_cast<uint8_t>(1u << IEC_CLK_IN_BIT);
                    }
                    // Atn In
                    if ((ddrB & (1u << IEC_ATN_IN_BIT)) == 0)
                    {
                        bool low = drive->getAtnLineLow();
                        if (low) value &= static_cast<uint8_t>(~(1u << IEC_ATN_IN_BIT));
                        else     value |= static_cast<uint8_t>(1u << IEC_ATN_IN_BIT);
                    }
                    // Device number DIP "switches"
                    int dev = drive->getDeviceNumber();
                    int offset = dev - 8;
                    if (offset < 0 || offset > 3) offset = 0; // Clamp value to reasonable number

                    // PB5 (DEV_BIT0)
                    if ((ddrB & (1u << IEC_DEV_BIT0)) == 0)   // only override when configured as input
                    {
                        if (offset & 0x01) value |=  (1u << IEC_DEV_BIT0);
                        else               value &= static_cast<uint8_t>(~(1u << IEC_DEV_BIT0));
                    }

                    // PB6 (DEV_BIT1)
                    if ((ddrB & (1u << IEC_DEV_BIT1)) == 0)
                    {
                        if (offset & 0x02) value |=  (1u << IEC_DEV_BIT1);
                        else               value &= static_cast<uint8_t>(~(1u << IEC_DEV_BIT1));
                    }
                }
            }
            if (viaRole == VIARole::VIA2_Mechanics)
            {
                if (auto* drive = dynamic_cast<D1571*>(parentPeripheral))
                {
                    // Bit 4: Write protect (input, active low)
                    if ((ddrB & (1u << MECH_WRITE_PROTECT)) == 0)
                    {
                        bool wpLow = drive->fdcIsWriteProtected(); // true if disk is write-protected
                        if (wpLow) value &= static_cast<uint8_t>(~(1u << MECH_WRITE_PROTECT)); // 0 = protected
                        else       value |=  static_cast<uint8_t> (1u << MECH_WRITE_PROTECT);  // 1 = writable
                    }

                    // Bit 7: Sync Detected
                    if ((ddrB & (1u << MECH_SYNC_DETECTED)) == 0)
                    {
                        bool syncLow = isSyncDetectedLow();
                        if (syncLow) value &= static_cast<uint8_t>(~(1u << MECH_SYNC_DETECTED));
                        else         value |= static_cast<uint8_t>(1u << MECH_SYNC_DETECTED);
                    }
                }
            }
            return value;
        }
        case 0x01:
        {
            uint8_t value = registers.oraIRA;
            uint8_t ddrA  = registers.ddrA;

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
            return value;
        }
        case 0x02: return registers.ddrB;
        case 0x03: return registers.ddrA;
        case 0x04: return registers.timer1CounterLowByte;
        case 0x05: return registers.timer1CounterHighByte;
        case 0x06: return registers.timer1LowLatch;
        case 0x07: return registers.timer1HighLatch;
        case 0x08: return registers.timer2CounterLowByte;
        case 0x09: return registers.timer2CounterHighByte;
        case 0x0A: return registers.serialShift;
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

                    // Bit 2: Motor Control
                    if (ddrB & (1u << MECH_SPINDLE_MOTOR))
                    {
                        bool enable = (registers.orbIRB & (1u << MECH_SPINDLE_MOTOR)) == 0;
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
                            drive->setFastSerialBusDirection(output);
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
                        drive->setFastSerialBusDirection(output);
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

void D1571VIA::setIECInputLines(bool atnLow, bool clkLow, bool dataLow)
{
    uint8_t oldIRB = registers.orbIRB;      // last input latch
    uint8_t newIRB = oldIRB;

    if (atnLow) newIRB &= ~(1 << IEC_ATN_IN_BIT);
    else        newIRB |=  (1 << IEC_ATN_IN_BIT);

    if (clkLow) newIRB &= ~(1 << IEC_CLK_IN_BIT);
    else        newIRB |=  (1 << IEC_CLK_IN_BIT);

    if (dataLow) newIRB &= ~(1 << IEC_DATA_IN_BIT);
    else         newIRB |=  (1 << IEC_DATA_IN_BIT);

    registers.orbIRB = newIRB;
}

void D1571VIA::updateIECOutputsFromPortB()
{
    if (viaRole != VIARole::VIA1_IECBus)
        return;

    auto* drive = dynamic_cast<Drive*>(parentPeripheral);
    if (!drive)
        return;

    const uint8_t orb  = registers.orbIRB;
    const uint8_t ddrB = registers.ddrB;

    // Only drive the line when the bit is OUTPUT (DDRB bit = 1)
    // and ORB bit = 0 => pull the line LOW.
    bool dataLow = false;
    bool clkLow  = false;

    // DATA OUT (PB0)
    if (ddrB & (1u << IEC_DATA_OUT_BIT))
    {
        dataLow = ((orb & (1u << IEC_DATA_OUT_BIT)) == 0);
    }

    // CLK OUT (PB1)
    if (ddrB & (1u << IEC_CLK_OUT_BIT))
    {
        clkLow = ((orb & (1u << IEC_CLK_OUT_BIT)) == 0);
    }

    drive->driveControlDataLine(dataLow);
    drive->driveControlClkLine(clkLow);
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
