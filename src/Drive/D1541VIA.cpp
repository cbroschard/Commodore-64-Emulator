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
    parentPeripheral(nullptr),
    viaRole(VIARole::Unknown),
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
    t1Counter(0),
    t1Latch(0),
    t1Running(false),
    t2Counter(0),
    t2Latch(0),
    t2Running(false),
    t1JustLoaded(false),
    t1ReloadPending(false),
    t1InhibitIRQ(false),
    t2JustLoaded(false),
    t2InhibitIRQ(false),
    t2LowLatchByte(0x00),
    t1PB7Level(true),
    busAtnLow(false),
    busClkLow(false),
    busDataLow(false),
    srCount(0),
    ca1Level(false),
    ca2Level(false),
    cb1Level(false),
    cb2Level(false)
{
    reset();
}

D1541VIA::~D1541VIA() = default;

void D1541VIA::attachPeripheralInstance(Peripheral* parentPeripheral, VIARole viaRole)
{
    this->parentPeripheral = parentPeripheral;
    this->viaRole = viaRole;
    if (viaRole == VIARole::VIA1_IECBus) updateIECOutputsFromPortB();
}

void D1541VIA::reset()
{
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

    // Timers
    t1Counter                           = 0x00;
    t1Latch                             = 0x00;
    t1Running                           = false;
    t2Counter                           = 0x00;
    t2Latch                             = 0x00;
    t2Running                           = false;
    t1JustLoaded                        = false;
    t1ReloadPending                     = false;
    t1InhibitIRQ                        = false;
    t2JustLoaded                        = false;
    t2InhibitIRQ                        = false;
    t2LowLatchByte                      = 0x00;
    t1PB7Level                          = true;

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

    // CA1 + CA2
    ca1Level                            = false;
    ca2Level                            = false;
    cb1Level                            = false;
    cb2Level                            = false;

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

void D1541VIA::tick(uint32_t cycles)
{
    while (cycles-- > 0)
    {
        // -----------------------
        // Timer 1 (T1)
        // -----------------------
        if (t1Running)
        {
            if (t1ReloadPending)
            {
                t1Counter = t1Latch;
                t1ReloadPending = false;
                t1JustLoaded = true;

                // mirror
                registers.timer1CounterLowByte  = static_cast<uint8_t>(t1Counter & 0xFF);
                registers.timer1CounterHighByte = static_cast<uint8_t>((t1Counter >> 8) & 0xFF);
            }

            // After a write to T1C-H, the first decrement happens on the NEXT PHI2.
            if (t1JustLoaded)
            {
                t1JustLoaded = false;
            }
            else
            {
                t1Counter = static_cast<uint16_t>(t1Counter - 1); // wraps naturally

                // mirror
                registers.timer1CounterLowByte  = static_cast<uint8_t>(t1Counter & 0xFF);
                registers.timer1CounterHighByte = static_cast<uint8_t>((t1Counter >> 8) & 0xFF);

                // Datasheet behavior: IFR sets when the counter reaches zero. :contentReference[oaicite:2]{index=2}
                if (t1Counter == 0x0000)
                {
                    triggerInterrupt(IFR_TIMER1);

                    const bool t1FreeRun = (registers.auxControlRegister & 0x40) != 0;
                    if (t1FreeRun)
                    {
                        // In free-run, reload from latch instead of running through 0 -> FFFF. :contentReference[oaicite:4]{index=4}
                        t1ReloadPending = true;
                    }
                }
            }
        }

        // -----------------------
        // Timer 2 (T2)
        // -----------------------
        if (t2Running)
        {
            const bool t2PulseCountMode = (registers.auxControlRegister & 0x20) != 0;

            if (!t2PulseCountMode)
            {
                // Interval mode: decrement each PHI2 after the initial load cycle.
                if (t2JustLoaded)
                {
                    t2JustLoaded = false;
                }
                else
                {
                    t2Counter = static_cast<uint16_t>(t2Counter - 1);

                    // mirror
                    registers.timer2CounterLowByte  = static_cast<uint8_t>(t2Counter & 0xFF);
                    registers.timer2CounterHighByte = static_cast<uint8_t>((t2Counter >> 8) & 0xFF);

                    // Datasheet: T2 provides a single interrupt per "write T2C-H",
                    // then disables further flag setting until rewritten. :contentReference[oaicite:5]{index=5}
                    if (t2Counter == 0x0000 && !t2InhibitIRQ)
                    {
                        triggerInterrupt(IFR_TIMER2);
                        t2InhibitIRQ = true;
                    }
                }
            }
            // Pulse-count mode is NOT decremented here by design.
        }
    }
}

uint8_t D1541VIA::readRegister(uint16_t address)
{
    switch(address)
    {
        case 0x00:
        {
            const uint8_t ddrB  = registers.ddrB;
            uint8_t value = static_cast<uint8_t>((registers.orbIRB & ddrB) | (portBPins & static_cast<uint8_t>(~ddrB)));

            clearIFR(IFR_CB1);

            const uint8_t cb2Mode = (registers.peripheralControlRegister >> 5) & 0x07; // PCR7..PCR5
            const bool cb2Independent = (cb2Mode == 0b001) || (cb2Mode == 0b011);
            if (!cb2Independent)
                clearIFR(IFR_CB2);

            if (viaRole == VIARole::VIA1_IECBus)
            {
                if (auto* drive = dynamic_cast<D1541*>(parentPeripheral))
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
                if (auto* drive = dynamic_cast<D1541*>(parentPeripheral))
                {
                    // WP input (active low)
                    if ((ddrB & (1u << MECH_WRITE_PROTECT)) == 0)
                    {
                        bool wpLow = drive->isWriteProtected();
                        if (wpLow) value &= static_cast<uint8_t>(~(1u << MECH_WRITE_PROTECT));
                        else       value |=  static_cast<uint8_t>( (1u << MECH_WRITE_PROTECT));
                    }

                    if ((ddrB & (1u << MECH_SYNC_DETECTED)) == 0)
                    {
                        bool sync = isSyncDetected();
                        if (sync)   value &= ~(1u << MECH_SYNC_DETECTED); // active-low: SYNC => 0
                        else        value |=  (1u << MECH_SYNC_DETECTED); // no sync => 1
                    }
                }
            }

            return value;
        }
        case 0x01: // ORA/IRA (Port A)
        {
            const uint8_t ddrA  = registers.ddrA;

            // Default: normal VIA port behavior
            uint8_t value = static_cast<uint8_t>(registers.oraIRA & ddrA);

            uint8_t inputPins = portAPins;

            if (viaRole == VIARole::VIA1_IECBus)
            {
                if (auto* drive = dynamic_cast<D1541*>(parentPeripheral))
                {
                    // The 1541 ROM steps the head back until this bit goes LOW.
                    if ((ddrA & (1u << PORTA_TRACK0_SENSOR)) == 0)
                    {
                        bool atTrack0 = drive->isTrack0();
                        if (atTrack0) inputPins &= static_cast<uint8_t>(~(1u << PORTA_TRACK0_SENSOR));
                        else          inputPins |= (1u << PORTA_TRACK0_SENSOR);
                    }

                    // The 1541 ROM polls this bit to know when to read VIA2.
                    if ((ddrA & (1u << PORTA_BYTE_READY)) == 0)
                    {
                        bool byteReadyLow = drive->getByteReadyLow(); // You added this to D1541.h
                        if (byteReadyLow) inputPins &= static_cast<uint8_t>(~(1u << PORTA_BYTE_READY));
                        else              inputPins |= (1u << PORTA_BYTE_READY);
                    }
                }
            }

            // Combine inputs with the DDR mask
            value |= (inputPins & static_cast<uint8_t>(~ddrA));

            if (viaRole == VIARole::VIA2_Mechanics)
            {
                value = static_cast<uint8_t>((registers.oraIRA & ddrA) | (mechDataLatch & static_cast<uint8_t>(~ddrA)));

                // Reading PRA consumes the pending byte and clears CA1 IFR
                if (mechBytePending)
                {
                    #ifdef Debug
                    std::cout << "[VIA2] PRA consume $" << std::hex << int(mechDataLatch) << std::dec << "\n";
                    #endif
                    mechBytePending = false;
                }
            }
            // Clear CA1 on ORA access
            clearIFR(IFR_CA1);

            // Clear CA2 too, EXCEPT in CA2 independent input mode
            const uint8_t ca2Mode = (registers.peripheralControlRegister >> 1) & 0x07; // PCR3..PCR1
            const bool ca2Independent = (ca2Mode == 0b001) || (ca2Mode == 0b011);      // independent input modes
            if (!ca2Independent)
                clearIFR(IFR_CA2);

            return value;
        }
        case 0x02: return registers.ddrB;
        case 0x03: return registers.ddrA;
        case 0x04:
        {
            clearIFR(IFR_TIMER1);
            return static_cast<uint8_t>(t1Counter & 0xFF);
        }
        case 0x05: return static_cast<uint8_t>((t1Counter >> 8) & 0xFF);
        case 0x06: return registers.timer1LowLatch;
        case 0x07: return registers.timer1HighLatch;
        case 0x08:
        {
            clearIFR(IFR_TIMER2);
            return static_cast<uint8_t>(t2Counter & 0xFF);
        }
        case 0x09: return static_cast<uint8_t>((t2Counter >> 8) & 0xFF);
        case 0x0A:
        {
            clearIFR(IFR_SR);
            if (viaRole == VIARole::VIA2_Mechanics)
            {
                return mechDataLatch;
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
        case 0x0F:
        {
            const uint8_t ddrA  = registers.ddrA;
            const uint8_t inputPins = portAPins;
            uint8_t value = (registers.oraIRA & ddrA) | (inputPins & (uint8_t)~ddrA);

            return value;
        }
        default: return 0xFF;
    }
}

void D1541VIA::writeRegister(uint16_t address, uint8_t value)
{
    switch(address)
    {
        case 0x00:
        {
            #ifdef Debug
            if (viaRole == VIARole::VIA1_IECBus)
            {
                std::cout << "[VIA1] ORB write: $" << std::hex << int(value)
                          << " (old=$" << int(registers.orbIRB) << ")"
                          << " DDRB=$" << int(registers.ddrB)
                          << std::dec << "\n";
            }
            #endif
            uint8_t prevORB  = registers.orbIRB;
            registers.orbIRB = value;

            if (viaRole == VIARole::VIA1_IECBus)
            {
                // Recompute IEC outputs based on (ORB, DDRB)
                updateIECOutputsFromPortB();
            }
            if (viaRole == VIARole::VIA2_Mechanics)
            {
                if (auto* drive = dynamic_cast<D1541*>(parentPeripheral))
                {
                    uint8_t ddrB = registers.ddrB;

                    // stepper phase (1541-style head move)
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

            // ORB write clears CB1 always, CB2 unless independent mode
            clearIFR(IFR_CB1);

            const uint8_t cb2Mode = (registers.peripheralControlRegister >> 5) & 0x07;
            const bool cb2Independent = (cb2Mode == 0b001) || (cb2Mode == 0b011);
            if (!cb2Independent)
                clearIFR(IFR_CB2);

            break;
        }
        case 0x01:
        {
            registers.oraIRA = value;

            clearIFR(IFR_CA1);

            const uint8_t ca2Mode = (registers.peripheralControlRegister >> 1) & 0x07;
            const bool ca2Independent = (ca2Mode == 0b001) || (ca2Mode == 0b011);
            if (!ca2Independent)
                clearIFR(IFR_CA2);

            break;
        }
        case 0x02:
        {
            #ifdef Debug
            if (viaRole == VIARole::VIA1_IECBus)
            {
                std::cout << "[VIA1] DDRB write: $" << std::hex << int(value)
                          << " (old=$" << int(registers.ddrB) << ")"
                          << " ORB=$" << int(registers.orbIRB)
                          << std::dec << "\n";
            }
            #endif
            registers.ddrB = value;

            if (viaRole == VIARole::VIA1_IECBus)
            {
                updateIECOutputsFromPortB(); // direction changes can assert/release lines
            }
            else if (viaRole == VIARole::VIA2_Mechanics)
            {
                if (auto* drive = dynamic_cast<D1541*>(parentPeripheral))
                {
                    const uint8_t orb  = registers.orbIRB;
                    const uint8_t ddrB = registers.ddrB;

                    // Motor
                    if (ddrB & (1u << MECH_SPINDLE_MOTOR))
                    {
                        bool enable = (orb & (1u << MECH_SPINDLE_MOTOR)) != 0;
                        enable ? drive->startMotor() : drive->stopMotor();
                    }

                    // LED
                    if (ddrB & (1u << MECH_LED))
                    {
                        setLed((orb & (1u << MECH_LED)) != 0);
                    }

                    // Density (require both bits as outputs to be “valid”)
                    const uint8_t densMask = (1u << MECH_DENSITY_BIT0) | (1u << MECH_DENSITY_BIT1);
                    if ((ddrB & densMask) == densMask)
                    {
                        uint8_t code =
                            ((orb >> MECH_DENSITY_BIT0) & 0x01) |
                            (((orb >> MECH_DENSITY_BIT1) & 0x01) << 1);
                        drive->setDensityCode(code);
                    }
                }
            }
            break;
        }
        case 0x03: registers.ddrA = value; break;
        case 0x04: // T1C-L
        {
            // On real 6522 this also updates the latch low byte.
            registers.timer1LowLatch       = value;

            t1Latch = (t1Latch & 0xFF00) | value;
            break;
        }
        case 0x05: // T1C-H (loads/starts)
        {
            // Update latch high from the value being written
            registers.timer1HighLatch = value;

            // Rebuild the full latch from high+low latch bytes
            t1Latch = (static_cast<uint16_t>(registers.timer1HighLatch) << 8) |
                      static_cast<uint16_t>(registers.timer1LowLatch);

            // Load counter from latch and start
            t1Counter = t1Latch;
            t1Running = true;

            // accuracy flags
            t1JustLoaded    = true;
            t1ReloadPending = false;
            t1InhibitIRQ    = false;

            clearIFR(IFR_TIMER1);

            // mirrors
            registers.timer1CounterLowByte  = static_cast<uint8_t>(t1Counter & 0xFF);
            registers.timer1CounterHighByte = static_cast<uint8_t>((t1Counter >> 8) & 0xFF);
            break;
        }
        case 0x06: // T1 Latch L
        {
            registers.timer1LowLatch = value;
            t1Latch = (t1Latch & 0xFF00) | value;
            break;
        }
        case 0x07: // T1 Latch H
        {
            registers.timer1HighLatch = value;
            t1Latch = (static_cast<uint16_t>(registers.timer1HighLatch) << 8) |
                      static_cast<uint16_t>(registers.timer1LowLatch);
            clearIFR(IFR_TIMER1);
            break;
        }
        case 0x08: // T2C-L
        {
            t2LowLatchByte = value;
            break;
        }
        case 0x09: // T2C-H (loads/starts)
        {
            registers.timer2CounterHighByte = value;

            t2Counter = (static_cast<uint16_t>(registers.timer2CounterHighByte) << 8) |
                        static_cast<uint16_t>(t2LowLatchByte);

            t2Latch   = t2Counter;
            t2Running = true;

            // NEW: accuracy flags
            t2JustLoaded = true;   // don't decrement until next PHI2
            t2InhibitIRQ = false;

            clearIFR(IFR_TIMER2);

            // mirrors
            registers.timer2CounterLowByte  = static_cast<uint8_t>(t2Counter & 0xFF);
            registers.timer2CounterHighByte = static_cast<uint8_t>((t2Counter >> 8) & 0xFF);

            break;
        }
        case 0x0A:
        {
            registers.serialShift = value;
            clearIFR(IFR_SR);
            resetShift();
            break;
        }
        case 0x0B:
        {
            #ifdef Debug
            std::cout << "Updated AUX Control Register with value: " << static_cast<int>(value) << "\n";
            #endif // Debug
            registers.auxControlRegister = value;
            break;
        }
        case 0x0C: // PCR
        {
            registers.peripheralControlRegister = value;
        #ifdef Debug
            std::cout << "[VIA1] PCR write: $" << hex2(value)
                      << "  CA1_edge=" << ((value & 0x01) ? "POS" : "NEG")
                      << "\n";
        #endif
            break;
        }
        case 0x0D:
        {
            uint8_t mask = value & 0x7F;
            // Clear any bits where a 1 was written
            clearIFR(mask);
            break;
        }
        case 0x0E: // IER
        {
            const bool set = (value & 0x80) != 0;
            const uint8_t mask = (value & 0x7F);
            if (set) registers.interruptEnable |= mask;
            else     registers.interruptEnable &= ~mask;

        #ifdef Debug
            std::cout << "[VIA1] IER write: $" << hex2(value)
                      << "  IER=$" << hex2(registers.interruptEnable)
                      << "\n";
        #endif
            refreshMasterBit();
            break;
        }
        case 0x0F:
        {
            registers.oraIRA = value;

            clearIFR(IFR_CA1);

            const uint8_t ca2Mode = (registers.peripheralControlRegister >> 1) & 0x07;
            const bool ca2Independent = (ca2Mode == 0b001) || (ca2Mode == 0b011);
            if (!ca2Independent)
                clearIFR(IFR_CA2);

            break;
        }
        default: break;
    }
}

void D1541VIA::diskByteFromMedia(uint8_t byte, bool inSync)
{
    if (viaRole != VIARole::VIA2_Mechanics) return;

    // Update sync marker state (PB7 handling happens in readRegister($00))
    setSyncDetected(inSync);

    // During SYNC marks, don't deliver bytes / don't generate byte-ready events
    if (inSync)
    {
        mechBytePending = false;
        clearIFR(IFR_CA1);
        return;
    }

    if (mechBytePending)
        return;

    mechDataLatch   = byte;
    mechBytePending = true;

    triggerInterrupt(IFR_CA1);

    // Pulse SO to set V flag for the BVC loop
    if (parentPeripheral)
    {
        auto* drive = static_cast<D1541*>(parentPeripheral);
        drive->asDrive()->getDriveCPU()->pulseSO();
    }
}

void D1541VIA::setIECInputLines(bool atnLow, bool clkLow, bool dataLow)
{
    if (viaRole != VIARole::VIA1_IECBus)
        return;

    const bool prevAtnLow = busAtnLow;
    const bool prevClkLow = busClkLow;

    busAtnLow  = atnLow;
    busClkLow  = clkLow;
    busDataLow = dataLow;

    // Detect clock edges from "clkLow"
    const bool clkRising  = (prevClkLow && !busClkLow); // low -> high
    const bool clkFalling = (!prevClkLow && busClkLow); // high -> low

    // --- 7474 ATN ACK latch ---
    const bool latchSetNow = (!prevAtnLow && busAtnLow); // ATN high->low

    if (latchSetNow)
    {
        // If PB4/ATNA clear is already asserted, the 7474 can't latch "set".
        if (!isAtnAckClearAsserted())
        {
            atnAckArmed = true;
            atnAckLatch = true;
        }
        else
        {
            atnAckArmed = false;
            atnAckLatch = false;
        }

        updateIECOutputsFromPortB();
    }


    // ATN low->high clears async
    if (prevAtnLow && !busAtnLow)
    {
        atnAckArmed = false;
        atnAckLatch = false;
    }

    if (!prevAtnLow && busAtnLow)
    {
        atnAckArmed = true;

        // NEW: if CLK is already HIGH, assert presence immediately
        if (!busClkLow && !isAtnAckClearAsserted())
        {
            atnAckLatch = true;
            atnAckArmed = false;
        }
    }

    // Existing: if ATN fell while CLK was low, latch on CLK rising
    if (atnAckArmed && prevClkLow && !busClkLow && !isAtnAckClearAsserted())
    {
        atnAckLatch = true;
        atnAckArmed = false;
    }

    setCA1Level(busAtnLow);

    uint8_t pins = portBPins;

    if (dataLow) pins |=  (1u << IEC_DATA_IN_BIT);
    else         pins &= ~(1u << IEC_DATA_IN_BIT);

    if (clkLow)  pins |=  (1u << IEC_CLK_IN_BIT);
    else         pins &= ~(1u << IEC_CLK_IN_BIT);

    if (atnLow)  pins |=  (1u << IEC_ATN_IN_BIT);
    else         pins &= ~(1u << IEC_ATN_IN_BIT);

    portBPins = pins;

    // Feed clock edges into the SR logic too
    if (clkRising || clkFalling)
        onClkEdge(clkRising, clkFalling);

    if ((busAtnLow != prevAtnLow) || (busClkLow != prevClkLow))
        updateIECOutputsFromPortB();
}

void D1541VIA::clearMechBytePending()
{
    mechBytePending = false;
    clearIFR(IFR_CA1);
}

void D1541VIA::setCA1Level(bool level)
{
    if (level == ca1Level) return;

    const bool old = ca1Level;
    ca1Level = level;

    const bool rising  = (!old &&  level);
    const bool falling = ( old && !level);

#ifdef Debug
    std::cout << "[VIA1] CA1 edge: " << (rising ? "RISING" : "FALLING")
              << "  PCR=$" << hex2(registers.peripheralControlRegister)
              << "  IFR=$" << hex2(registers.interruptFlag)
              << "  IER=$" << hex2(registers.interruptEnable)
              << "\n";
#endif

    onCA1Edge(rising, falling);
}

void D1541VIA::setCA2Level(bool level)
{
    if (level == ca2Level) return;
    bool rising  = (!ca2Level && level);
    bool falling = (ca2Level && !level);
    ca2Level = level;

    onCA2Edge(rising, falling);
}

void D1541VIA::setCB2Level(bool level)
{
    if (level == cb2Level) return;

    bool rising  = (!cb2Level && level);
    bool falling = ( cb2Level && !level);
    cb2Level = level;

    onCB2Edge(rising, falling);
}

void D1541VIA::setCB1Level(bool level)
{
    if (level == cb1Level) return;

    bool rising  = (!cb1Level && level);
    bool falling = ( cb1Level && !level);
    cb1Level = level;

    onCB1Edge(rising, falling);
}

bool D1541VIA::checkIRQActive() const
{
    uint8_t active = registers.interruptEnable & registers.interruptFlag & 0x7F;
    return active != 0;
}

void D1541VIA::triggerInterrupt(uint8_t mask)
{
    registers.interruptFlag |= mask;
    refreshMasterBit();
}

void D1541VIA::clearIFR(uint8_t mask)
{
    registers.interruptFlag &= static_cast<uint8_t>(~mask);
    refreshMasterBit();
}

void D1541VIA::refreshMasterBit()
{
    const uint8_t pendingEnabled = registers.interruptFlag & registers.interruptEnable & 0x7F;

    if (pendingEnabled)
        registers.interruptFlag |= IFR_IRQ;
    else
        registers.interruptFlag &= static_cast<uint8_t>(~IFR_IRQ);
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

void D1541VIA::onCA1Edge(bool rising, bool falling)
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
        triggerInterrupt(IFR_CA1);
}

void D1541VIA::onCA2Edge(bool rising, bool falling)
{
    // PCR bits 3..1 = CA2 control
    // If PCR bit 3 == 0 => CA2 is INPUT (interrupt capable)
    // In input mode:
    //   000 = negative edge, 001 = positive edge  (PCR bit 1 selects)
    const uint8_t pcr = registers.peripheralControlRegister;

    const bool ca2IsInput = (pcr & 0x08) == 0;     // bit 3
    if (!ca2IsInput) return;                       // output/handshake modes: no CA2 input IRQ

    const bool activeEdgePos = (pcr & 0x02) != 0;  // bit 1
    bool trigger = (activeEdgePos && rising) || (!activeEdgePos && falling);

    if (trigger)
        triggerInterrupt(IFR_CA2);
}

void D1541VIA::onCB1Edge(bool rising, bool falling)
{
    // PCR Bit 4 controls CB1 Active Edge
    // 0 = Negative Edge (High to Low)
    // 1 = Positive Edge (Low to High)
    const uint8_t pcr = registers.peripheralControlRegister;

    const bool activeEdgePos = (pcr & 0x10) != 0; // bit 4
    bool trigger = false;

    if (activeEdgePos && rising)
        trigger = true;
    else if (!activeEdgePos && falling)
        trigger = true;

    if (trigger)
        triggerInterrupt(IFR_CB1);
}

void D1541VIA::onCB2Edge(bool rising, bool falling)
{
    // PCR bits 7..5 = CB2 control
    // If PCR bit 7 == 0 => CB2 is INPUT (interrupt capable)
    // In input mode:
    //   000 = negative edge, 001 = positive edge  (PCR bit 5 selects)
    const uint8_t pcr = registers.peripheralControlRegister;

    const bool cb2IsInput = (pcr & 0x80) == 0;    // bit 7
    if (!cb2IsInput)
        return;

    const bool activeEdgePos = (pcr & 0x20) != 0; // bit 5
    bool trigger = (activeEdgePos && rising) || (!activeEdgePos && falling);

    if (trigger)
        triggerInterrupt(IFR_CB2);
}

void D1541VIA::updateIECOutputsFromPortB()
{
    if (!parentPeripheral || viaRole != VIARole::VIA1_IECBus)
        return;

    auto* drive = static_cast<D1541*>(parentPeripheral);

    const uint8_t ddrB = registers.ddrB;
    const uint8_t orb  = registers.orbIRB;

    // VIA-driven DATA (normal serial output)
    const bool dataOutLow =
        (ddrB & (1u << IEC_DATA_OUT_BIT)) &&
        ((orb & (1u << IEC_DATA_OUT_BIT)) != 0);

    // VIA-driven CLK
    const bool clkOutLow =
        (ddrB & (1u << IEC_CLK_OUT_BIT)) &&
        ((orb & (1u << IEC_CLK_OUT_BIT)) != 0);

    const bool atnAckClearActive = isAtnAckClearAsserted();

    // LEVEL-SENSITIVE: if clear is asserted, latch cannot be set.
    if (atnAckClearActive)
    {
        #ifdef Debug
        std::cout << "atnAckClearActive hit\n";
        #endif // Debug
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

    return pb4IsOutput && pb4High;
}
