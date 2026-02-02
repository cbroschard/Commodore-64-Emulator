// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "MLMonitorBackend.h"
#include "IECBUS.h"
#include "Peripheral.h"

MLMonitorBackend::MLMonitorBackend() :
    cart(nullptr),
    cass(nullptr),
    cia1object(nullptr),
    cia2object(nullptr),
    processor(nullptr),
    bus(nullptr),
    logger(nullptr),
    pla(nullptr),
    sidchip(nullptr),
    vicII(nullptr)
{

}

MLMonitorBackend::~MLMonitorBackend() = default;

void MLMonitorBackend::detachCartridge()
{
    if (comp) comp->setCartridgeAttached(false);
}

bool MLMonitorBackend::getCartridgeAttached()
{
    if (comp) return comp->getCartridgeAttached();
    else return false;
}

void MLMonitorBackend::vicFFRaster(uint8_t targetRaster)
{
    while(vicII->getCurrentRaster() != targetRaster)
    {
        vicII->tick(1);
        processor->tick();
        cia1object->updateTimers(1);
        cia2object->updateTimers(1);
        sidchip->tick(1);
    }
}

void MLMonitorBackend::enterMonitor()
{
    if (comp) comp->enterMonitor();
}

void MLMonitorBackend::coldReset()
{
    if (comp) comp->coldReset();
    else std::cerr << "Error: No Computer attached, cannot perform reset!\n";
}

void MLMonitorBackend::warmReset()
{
    if (comp) comp->warmReset();
    else std::cerr << "Error: No Computer attached, cannot perform reset!\n";
}

void MLMonitorBackend::irqDisableAll()
{
    if (!vicII && !cia1object && !cia2object) return;

    snapshot.has = true;
    snapshot.vic  = vicII->snapshotIRQs();
    snapshot.cia1 = cia1object->snapshotIRQs();
    snapshot.cia2 = cia2object->snapshotIRQs();

    vicII->disableAllIRQs();
    cia1object->disableAllIRQs();
    cia2object->disableAllIRQs();

    irqClearAll();  // acknowledge anything pending after the mask change
}

void MLMonitorBackend::irqClearAll()
{
    if (!vicII && !cia1object && !cia2object) return;

    vicII->clearPendingIRQs();
    cia1object->clearPendingIRQs();
    cia2object->clearPendingIRQs();
}

void MLMonitorBackend::irqRestore()
{
    if (!vicII && !cia1object && !cia2object) return;
    if (!snapshot.has) return;

    vicII->restoreIRQs(snapshot.vic);
    cia1object->restoreIRQs(snapshot.cia1);
    cia2object->restoreIRQs(snapshot.cia2);
}

void MLMonitorBackend::setLogging(LogSet log, bool enabled)
{
    switch (log)
    {
        case LogSet::Cartridge: if (cart) cart->setLog(enabled); break;
        case LogSet::Cassette: if (cass) cass->setLog(enabled); break;
        case LogSet::CIA1: if (cia1object) cia1object->setLog(enabled); break;
        case LogSet::CIA2: if (cia2object) cia2object->setLog(enabled); break;
        case LogSet::CPU: if (processor) processor->setLog(enabled); break;
        case LogSet::IO: if (IO_adapter) IO_adapter->setLog(enabled); break;
        case LogSet::Joystick:
        {
            Joystick* joy1 = comp->getJoy1();
            if (joy1)
            {
                joy1->attachLogInstance(logger);
                joy1->setLog(enabled);
            }

            Joystick* joy2 = comp->getJoy2();
            if (joy2)
            {
                joy2->attachLogInstance(logger);
                joy2->setLog(enabled);
            }
            break;
        }
        case LogSet::Keyboard: if (keyb) keyb->setLog(enabled); break;
        case LogSet::Memory: if (mem) mem->setLog(enabled); break;
        case LogSet::PLA: if (pla) pla->setLog(enabled); break;
        case LogSet::VIC: if (vicII) vicII->setLog(enabled); break;
    }
}

void MLMonitorBackend::setJamMode(const std::string& mode)
{
    if (processor)
    {
        if (mode == "freeze")
        {
            processor->setJamMode(CPU::JamMode::FreezePC);
        }
        else if (mode == "halt")
        {
            processor->setJamMode(CPU::JamMode::Halt);
        }
        else if (mode == "nop")
        {
            processor->setJamMode(CPU::JamMode::NopCompat);
        }
    }
}

void MLMonitorBackend::dumpDriveList()
{
    if (!bus)
    {
        std::cout << "No IEC bus attached.\n";
        return;
    }

    const auto& devs = bus->getDevices();  // map<int, Peripheral*>

    if (devs.empty())
    {
        std::cout << "No IEC devices registered.\n";
        return;
    }

    std::cout << "IEC Devices:\n";
    std::cout << "  ID   Type      Details                          Status      Track   Sector\n";
    std::cout << "  --   -------   ---------------------------      -------     -----   ------\n";

    for (const auto& [id, dev] : devs)
    {
        if (!dev)
            continue;

        if (dev->isDrive())
        {
            // Grab a pointer to the drive so we can run drive only methods
            auto drv = dev->asDrive();
            if (!drv)
            {
                std::cerr << "Error: Not a drive\n";
            }

            // Output drive ID, type, and disk image name
            std::string img = drv->getLoadedDiskName();
            std::cout << "  " << id
                      << "    " << drv->getDriveTypeName();

            if (!img.empty())
                std::cout << "       [" << img << "]";

            // Output drive status and current track and sector
            std::string currentStatus = decodeDriveStatus(drv->getDriveStatus());
            std::cout << "    " << currentStatus << "          " << static_cast<int>(drv->getCurrentTrack());
            std::cout << "        " << static_cast<int>(drv->getCurrentSector()) << "\n";
        }
        else
        {
            std::cout << "  " << id << "    (non-drive IEC device)\n";
        }
    }
}

void MLMonitorBackend::dumpDriveSummary(int id)
{
    if (!bus)
    {
        std::cout << "No IEC bus attached.\n";
        return;
    }

    Peripheral* dev = bus->getDevice(id);

    if (!dev)
    {
        std::cout << "No such device with ID:" << id << "\n";
        return;
    }

    if (!dev->isDrive())
    {
        std::cout << "Device is not a Floppy Drive\n";
        return;
    }

    Drive* drive = dev->asDrive();

    // Get Drive status
    std::string currentStatus = decodeDriveStatus(dev->asDrive()->getDriveStatus());

    std::stringstream oss;
    oss << "Drive " << id << " Summary:\n";
    oss << "  Type:        " << drive->getDriveTypeName() << "\n";
    oss << "  Image:       " << drive->getLoadedDiskName() << "\n";
    oss << "  Disk Loaded: " << (drive->isDiskLoaded() ? "Yes" : "No") << "\n\n";
    oss << "  Track:       " << static_cast<int>(drive->getCurrentTrack()) << "\n";
    oss << "  Sector:      " << static_cast<int>(drive->getCurrentSector()) << "\n";
    oss << "  Motor:       " << (drive->isMotorOn() ? "On" : "Off") << "\n\n";
    oss << "  ATN Line:    " << (drive->getAtnLineLow() ? "Low" : "High") << "\n";
    oss << "  CLK Line:    " << (drive->getClkLineLow() ? "Low" : "High") << "\n";
    oss << "  DATA Line:   " << (drive->getDataLineLow() ? "Low" : "High") << "\n\n";
    oss << "  Status:      " << currentStatus << "\n";
    std::cout << oss.str();
}

void MLMonitorBackend::dumpDriveCIA(int id)
{
    if (!bus)
    {
        std::cout << "No IEC bus attached.\n";
        return;
    }

    Peripheral* dev = bus->getDevice(id);

    if (!dev)
    {
        std::cout << "No such device with ID:" << id << "\n";
        return;
    }

    if (!dev->isDrive())
    {
        std::cout << "Device is not a Floppy Drive\n";
        return;
    }

    Drive* drive = dev->asDrive();

    if (!drive->hasCIA())
    {
        std::cout << "Device has no CIA chip\n";
        return;
    }

    auto* cia = dev->asDrive()->getCIA();
    auto registers = cia->getRegsView();

    std::stringstream oss;
    oss << "CIA Registers:\n";
    oss << std::left;

    // Ports
    oss << "  PORTA:  " << std::setw(10) << "" << "$" << hex2(registers.portA)
        << std::setw(6) << "" << "PORTB:  " << std::setw(10) << ""
        << "$" << hex2(registers.portB) << "\n";

    oss << "  DDRA:   " << std::setw(10) << "" << "$" <<hex2(registers.ddrA)
        << std::setw(4) << ""
        << "  DDRB:   " << std::setw(10) << "" << "$" << hex2(registers.ddrB) << "\n";

    // Timer bytes
    oss << "  TimerA Low Byte:  $" << hex2(registers.tAL)
        << std::setw(6) << ""
        << "TimerA High Byte: $" << hex2(registers.tAH) << "\n";

    oss << "  TimerB Low Byte:  $" << hex2(registers.tBL)
        << std::setw(6) << ""
        << "TimerB High Byte: $" << hex2(registers.tBH) << "\n";

    // Timer words
    oss << "  TimerA Counter:   $" << hex4(registers.tA)
        << std::setw(4) << "" << "TimerA Latch:  "
        << std::setw(3) << "" << "$" << hex4(registers.taLAT) << "\n";

    oss << "  TimerB Counter:   $" << hex4(registers.tB)
        << std::setw(4) << "" << "TimerB Latch:  "
        << std::setw(3) << "" << "$" << hex4(registers.tbLAT) << "\n";

    // TOD
    oss << "  TOD 10ths:        $" << hex2(registers.tod10)
    << std::setw(6) << ""
    << "TOD Seconds:      $" << hex2(registers.todSec) << "\n";

    oss << "  TOD Minutes:      $" << hex2(registers.todMin)
        << std::setw(6) << ""
        << "TOD Hours:        $" << hex2(registers.todHour) << "\n";

    // Misc (aligned 2-column rows)
    oss << "  Serial Data:      $" << hex2(registers.sd)
        << std::setw(6) << ""
        << "IER:              $" << hex2(registers.ier) << "\n";

    oss << "  CRA:              $" << hex2(registers.cra)
        << std::setw(6) << ""
        << "CRB:              $" << hex2(registers.crb) << "\n";

    std::cout << oss.str();
}

void MLMonitorBackend::dumpDriveCPU(int id)
{
    if (!bus)
    {
        std::cout << "No IEC bus attached.\n";
        return;
    }

    Peripheral* dev = bus->getDevice(id);

    if (!dev)
    {
        std::cout << "No such device with ID:" << id << "\n";
        return;
    }

    if (!dev->isDrive())
    {
        std::cout << "Device is not a Floppy Drive\n";
        return;
    }

    const CPU* cpu = dev->asDrive()->getDriveCPU();
    if (!cpu)
    {
        std::cout << "No CPU!\n";
        return;
    }

    // Get current cpu state
    CPUState st = cpu->getState();

    auto hex2 = [](uint32_t v){
        std::ostringstream s;
        s << std::uppercase << std::hex << std::setw(2)
          << std::setfill('0') << (v & 0xFF);
        return s.str();
    };
    auto hex4 = [](uint32_t v){
        std::ostringstream s;
        s << std::uppercase << std::hex << std::setw(4)
          << std::setfill('0') << (v & 0xFFFF);
        return s.str();
    };
    auto flagsBits = [&](uint8_t p){
        std::string b;
        b += (p & 0x80) ? '1' : '0'; // N
        b += (p & 0x40) ? '1' : '0'; // V
        b += '-';                    // (unused)
        b += (p & 0x10) ? '1' : '0'; // B
        b += (p & 0x08) ? '1' : '0'; // D
        b += (p & 0x04) ? '1' : '0'; // I
        b += (p & 0x02) ? '1' : '0'; // Z
        b += (p & 0x01) ? '1' : '0'; // C
        return b;
    };

    // NEW: read opcode at PC
    uint8_t op = cpu->debugRead(st.PC);

    std::ostringstream out;
    out << "Drive " << id << " CPU:\n";
    out << "PC=$" << hex4(st.PC)
        << "  A=$" << hex2(st.A)
        << "  X=$" << hex2(st.X)
        << "  Y=$" << hex2(st.Y)
        << "  SP=$" << hex2(st.SP)
        << "  P=$"  << hex2(st.SR)
        << "  (NV-BDIZC=" << flagsBits(st.SR) << ")\n";
    out << "OP=$" << hex2(op) << "\n";

    std::cout << out.str();
}

void MLMonitorBackend::driveCPUStep(int id)
{
    if (!bus)
    {
        std::cout << "No IEC bus attached.\n";
        return;
    }

    Peripheral* dev = bus->getDevice(id);

    if (!dev)
    {
        std::cout << "No such device with ID:" << id << "\n";
        return;
    }

    if (!dev->isDrive())
    {
        std::cout << "Device is not a Floppy Drive\n";
        return;
    }

    CPU* cpu = dev->asDrive()->getDriveCPU();
    if (!cpu)
    {
        std::cout << "No CPU!\n";
        return;
    }

    auto* mem = dev->asDrive()->getMemory();
    uint16_t pc = cpu->getPC();

    // Output disassembly at PC
    std::string disASM = Disassembler::disassembleAt(pc, *mem);
    std::cout << disASM << std::endl;

    // Execute tick to step
    cpu->tick();
    uint32_t cycles = cpu->getElapsedCycles();
    dev->asDrive()->getMemory()->tick(cycles);

    // Dump CPU registers
    auto st = cpu->getState();
    auto hex2 = [](uint32_t v){
        std::ostringstream s;
        s << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << (v & 0xFF);
        return s.str();
    };
    auto hex4 = [](uint32_t v){
        std::ostringstream s;
        s << std::uppercase << std::hex << std::setw(4) << std::setfill('0') << (v & 0xFFFF);
        return s.str();
    };
    auto flagsBits = [&](uint8_t p){
        std::string b;
        b += (p & 0x80) ? '1' : '0'; // N
        b += (p & 0x40) ? '1' : '0'; // V
        b += '-';                    // (unused)
        b += (p & 0x10) ? '1' : '0'; // B
        b += (p & 0x08) ? '1' : '0'; // D
        b += (p & 0x04) ? '1' : '0'; // I
        b += (p & 0x02) ? '1' : '0'; // Z
        b += (p & 0x01) ? '1' : '0'; // C
        return b;
    };

    std::ostringstream out;
    out << "PC=$" << hex4(st.PC)
         << "  A=$" << hex2(st.A)
         << "  X=$" << hex2(st.X)
         << "  Y=$" << hex2(st.Y)
         << "  SP=$" << hex2(st.SP)
         << "  P=$" << hex2(st.SR)
         << "  (NV-BDIZC=" << flagsBits(st.SR) << ")\n";

    std::cout << out.str();
}

void MLMonitorBackend::dumpDriveMemory(int id, uint16_t startAddress, uint16_t count)
{
    // Define the default display count if count is 0
    const uint16_t DEFAULT_COUNT = 16;
    uint16_t bytesToDump = (count == 0) ? DEFAULT_COUNT : count;

    if (!bus)
    {
        std::cout << "No IEC bus attached.\n";
        return;
    }

    Peripheral* dev = bus->getDevice(id);

    if (!dev)
    {
        std::cout << "No such device with ID:" << id << "\n";
        return;
    }

    if (!dev->isDrive())
    {
        std::cout << "Device is not a Floppy Drive\n";
        return;
    }

    auto* mem = dev->asDrive()->getMemory();

    if (!mem)
    {
        std::cout << "No memory device.\n";
        return;
    }

    // Use a stream for formatted output
    std::stringstream oss;
    oss << "Drive " << id << " Memory Dump ($"
        << hex4(startAddress) << " for " << bytesToDump << " bytes):\n";

    // Set up formatting for hex values
    oss << std::uppercase << std::hex << std::setfill('0');

    uint16_t currentAddress = startAddress;
    uint16_t bytesRead = 0;

    while (bytesRead < bytesToDump)
    {
        // Print the starting address of the current line
        oss << "$" << std::setw(4) << currentAddress << ": ";

        // Buffer for ASCII representation
        std::string ascii;

        // Print 8 bytes per line
        for (int i = 0; i < 8; ++i)
        {
            if (bytesRead >= bytesToDump)
            {
                // Fill remaining space if the last line is short
                oss << "   ";
            }
            else
            {
                uint8_t value = mem->read(currentAddress);
                oss << std::setw(2) << static_cast<int>(value) << " ";

                // Append to ASCII string
                if (value >= 0x20 && value <= 0x7E)
                {
                    ascii += static_cast<char>(value);
                }
                else
                {
                    ascii += '.'; // Non-printable character
                }

                currentAddress++;
                bytesRead++;
            }
        }

        // Print the ASCII representation
        oss << " " << ascii << "\n";
    }

    std::cout << oss.str();
}

void MLMonitorBackend::dumpDriveIECState(int id)
{
    if (!bus)
    {
        std::cout << "No IEC bus attached.\n";
        return;
    }

    Peripheral* dev = bus->getDevice(id);

    if (!dev)
    {
        std::cout << "No such device with ID:" << id << "\n";
        return;
    }

    if (!dev->isDrive())
    {
        std::cout << "Device is not a Floppy Drive\n";
        return;
    }

    Drive* d = dev->asDrive();
    const Drive::IECSnapshot s = d->snapshotIEC();

    auto HL = [](bool low) { return low ? "L" : "H"; };
    auto yn = [](bool v) { return v ? "1" : "0"; };

    // Pretty name for bus state
    auto busStateToStr = [](Drive::DriveBusState st) {
        switch (st)
        {
            case Drive::DriveBusState::IDLE:             return "IDLE";
            case Drive::DriveBusState::AWAITING_COMMAND: return "AWAITING_COMMAND";
            case Drive::DriveBusState::LISTENING:        return "LISTENING";
            case Drive::DriveBusState::TALKING:          return "TALKING";
            default:                                     return "UNKNOWN";
        }
    };

    std::cout << "Drive #" << id << " IEC state:\n";

    std::cout << "  Lines (seen):        "
              << "ATN="  << HL(s.atnLow)  << "  "
              << "CLK="  << HL(s.clkLow)  << "  "
              << "DATA=" << HL(s.dataLow) << "  "
              << "SRQ="  << HL(s.srqLow)  << "\n";

    std::cout << "  Drive drives(low?):  "
              << "ATN="  << HL(s.drvAssertAtn)  << "  "
              << "CLK="  << HL(s.drvAssertClk)  << "  "
              << "DATA=" << HL(s.drvAssertData) << "  "
              << "SRQ="  << HL(s.drvAssertSrq)  << "\n";

    std::cout << "  Mode: " << busStateToStr(s.busState)
              << "   listen=" << yn(s.listening)
              << " talk=" << yn(s.talking)
              << "   SA=";

    if (s.secondaryAddress < 0) std::cout << "(none)\n";
    else                        std::cout << "$" << hex2(static_cast<uint8_t>(s.secondaryAddress)) << "\n";

    std::cout << "  Shifter: shift=$" << hex2(s.shiftReg)
              << " bits=" << s.bitsProcessed << "\n";

    std::cout << "  Handshake: waitingForAck=" << yn(s.waitingForAck)
              << " ackEdgeCountdown=" << s.ackEdgeCountdown
              << " waitingForClkRelease=" << yn(s.waitingForClkRelease)
              << " prevClkLevel=" << yn(s.prevClkLevel)
              << "\n";

    std::cout << "            ackHold=" << yn(s.ackHold)
              << " byteAckHold=" << yn(s.byteAckHold)
              << " ackDelay=" << s.ackDelay
              << " swallowFalling=" << yn(s.swallowPostHandshakeFalling)
              << "\n";

    std::cout << "  Talk queue: " << s.talkQueueLen << "\n";
}

void MLMonitorBackend::dumpDriveVIA1(int id)
{
    if (!bus)
    {
        std::cout << "No IEC bus attached.\n";
        return;
    }

    Peripheral* dev = bus->getDevice(id);

    if (!dev)
    {
        std::cout << "No such device with ID:" << id << "\n";
        return;
    }

    if (!dev->isDrive())
    {
        std::cout << "Device is not a Floppy Drive\n";
        return;
    }

    if (!dev->asDrive()->hasVIA1())
    {
        std::cout << "Drive does not have a VIA1 chip\n";
        return;
    }

    auto* via1 = dev->asDrive()->getVIA1();
    if (!via1)
    {
        std::cout << "No VIA1!\n";
        return;
    }

    auto registers = via1->getRegsView();

    std::stringstream oss;
    oss << "VIA1 Registers:\n";
    oss << "  ORB: $" << hex2(registers.orbIRB)
        << "  ORA: $" << hex2(registers.oraIRA) << "\n";
    oss << "  DDRB: $" << hex2(registers.ddrB)
        << "  DDRA: $" << hex2(registers.ddrA) << "\n";

    // Timers as bytes
    oss << "  T1 Low Byte:  $" << hex2(registers.t1CL)
        << "  T1 High Byte: $" << hex2(registers.t1CH) << "\n";
    oss << "  T1 Latch Low: $" << hex2(registers.t1LL)
        << "  T1 Latch High:$" << hex2(registers.t1LH) << "\n";
    oss << "  T2 Low Byte:  $" << hex2(registers.t2CL)
        << "  T2 High Byte: $" << hex2(registers.t2CH) << "\n";

    // Timers as 16-bit words
    oss << "  T1 Counter:   $" << hex4(registers.t1CH, registers.t1CL)
        << "  T1 Latch: $"    << hex4(registers.t1LH, registers.t1LL) << "\n";
    oss << "  T2 Counter:   $" << hex4(registers.t2CH, registers.t2CL) << "\n";

    oss << "  Serial Shift: $" << hex2(registers.sr) << "\n";
    oss << "  Aux Control:  $" << hex2(registers.acr) << "\n";
    oss << "  Periph Ctrl:  $" << hex2(registers.pcr) << "\n";
    oss << "  IFR:          $" << hex2(registers.ifr) << "\n";
    oss << "  IER:          $" << hex2(registers.ier) << "\n";
    oss << "  ORA(no HS):   $" << hex2(registers.oraNoHS) << "\n";

    // Is this VIA pulling IRQ?
    bool irqActive = via1->checkIRQActive();
    oss << "  IRQ Active:   " << (irqActive ? "YES" : "NO") << "\n";

    std::cout << oss.str();
}

void MLMonitorBackend::dumpDriveVIA2(int id)
{
    if (!bus)
    {
        std::cout << "No IEC bus attached.\n";
        return;
    }

    Peripheral* dev = bus->getDevice(id);

    if (!dev)
    {
        std::cout << "No such device with ID:" << id << "\n";
        return;
    }

    if (!dev->isDrive())
    {
        std::cout << "Device is not a Floppy Drive\n";
        return;
    }

    if (!dev->asDrive()->hasVIA2())
    {
        std::cout << "Drive does not have a VIA2 chip\n";
        return;
    }

    auto* via2 = dev->asDrive()->getVIA2();
    if (!via2)
    {
        std::cout << "No VIA2!\n";
        return;
    }

    auto registers = via2->getRegsView();
    auto mech  = via2->getMechanicsInfo();

    std::stringstream oss;
    oss << "VIA2 Registers (Mechanics):\n";
    oss << "  ORB: $" << hex2(registers.orbIRB)
        << "  ORA: $" << hex2(registers.oraIRA) << "\n";
    oss << "  DDRB: $" << hex2(registers.ddrB)
        << "  DDRA: $" << hex2(registers.ddrA) << "\n";

    // Timers as bytes
    oss << "  T1 Low Byte:   $" << hex2(registers.t1CL)
        << "  T1 High Byte:  $" << hex2(registers.t1CH) << "\n";
    oss << "  T1 Latch Low:  $" << hex2(registers.t1LL)
        << "  T1 Latch High: $" << hex2(registers.t1LH) << "\n";
    oss << "  T2 Low Byte:   $" << hex2(registers.t2CL)
        << "  T2 High Byte:  $" << hex2(registers.t2CH) << "\n";

    // Timers as 16-bit words
    oss << "  T1 Counter:    $" << hex4(registers.t1CH, registers.t1CL)
        << "  T1 Latch: $"      << hex4(registers.t1LH, registers.t1LL) << "\n";
    oss << "  T2 Counter:    $" << hex4(registers.t2CH, registers.t2CL) << "\n";

    oss << "  Serial Shift:  $" << hex2(registers.sr)  << "\n";
    oss << "  Aux Control:   $" << hex2(registers.acr) << "\n";
    oss << "  Periph Ctrl:   $" << hex2(registers.pcr) << "\n";
    oss << "  IFR:           $" << hex2(registers.ifr) << "\n";
    oss << "  IER:           $" << hex2(registers.ier) << "\n";
    oss << "  ORA(no HS):    $" << hex2(registers.oraNoHS) << "\n";

    // Mechanics decode from Port B
    if (mech.valid)
    {
        oss << "\nMechanics (decoded):\n";
        oss << "  Motor:   " << (mech.motorOn ? "ON" : "OFF") << "\n";
        oss << "  LED:     " << (mech.ledOn ? "ON" : "OFF") << "\n";
        oss << "  Density: code=" << static_cast<unsigned>(mech.densityCode)
            << " (0-3)\n";
    }
    else
    {
        oss << "\nMechanics: (no mechanics info for this VIA)\n";
    }

    bool irqActive = via2->checkIRQActive();
    oss << "  IRQ Active: " << (irqActive ? "YES" : "NO") << "\n";

    std::cout << oss.str();
}

void MLMonitorBackend::dumpDriveFDC(int id)
{
    if (!bus)
    {
        std::cout << "No IEC bus attached.\n";
        return;
    }

    Peripheral* dev = bus->getDevice(id);

    if (!dev)
    {
        std::cout << "No such device with ID:" << id << "\n";
        return;
    }

    if (!dev->isDrive())
    {
        std::cout << "Device is not a Floppy Drive\n";
        return;
    }

    if (!dev->asDrive()->hasFDC())
    {
        std::cout << "Drive does not have a FDC\n";
        return;
    }

    auto* fdc = dev->asDrive()->getFDC();

    if (!fdc)
    {
        std::cout << "No FDC\n";
        return;
    }

    auto registers = fdc->getRegsView();

    auto yn = [](bool b){ return b ? "Y" : "N"; };

    // FDC177x Status bit masks (from your header)
    auto st = [&](uint8_t mask){ return (registers.status & mask) != 0; };

    auto decodeCmd = [](uint8_t cmd) -> const char*
    {
        switch (cmd & 0xF0)
        {
            case 0x00: return "RESTORE (I)";
            case 0x10: return "SEEK (I)";
            case 0x20: return "STEP (I)";
            case 0x40: return "STEP IN (I)";
            case 0x60: return "STEP OUT (I)";
            case 0x80: return "READ SECTOR (II)";
            case 0xA0: return "WRITE SECTOR (II)";
            case 0xC0: return "READ ADDRESS (III)";
            case 0xD0: return "FORCE INT (IV)";
            case 0xE0: return "READ TRACK (III)";
            case 0xF0: return "WRITE TRACK (III)";
            default:   return "UNKNOWN";
        }
    };

    auto hex4 = [](uint16_t v){
        std::ostringstream s;
        s << std::uppercase << std::hex << std::setw(4) << std::setfill('0') << v;
        return s.str();
    };

    std::stringstream oss;

    oss << "FDC Registers:\n";
    oss << "  Status:  $" << hex2(registers.status)
        << "  [BUSY=" << yn(st(0x01))
        << " DRQ="    << yn(st(0x02))
        << " LOST/T0="<< yn(st(0x04))
        << " CRC="    << yn(st(0x08))
        << " RNF="    << yn(st(0x10))
        << " SPIN/DD="<< yn(st(0x20))
        << " WP="     << yn(st(0x40))
        << " MOTOR="  << yn(st(0x80))
        << "]\n";

    oss << "  Command: $" << hex2(registers.command)
        << "  " << decodeCmd(registers.command) << "\n";

    oss << "  Track:   $" << hex2(registers.track)
        << " (" << std::dec << static_cast<unsigned>(registers.track) << ")\n";

    oss << "  Sector:  $" << hex2(registers.sector)
        << " (" << std::dec << static_cast<unsigned>(registers.sector) << ")\n";

    oss << "  Data:    $" << hex2(registers.data)
        << " (" << std::dec << static_cast<unsigned>(registers.data) << ")\n";

    oss << "  DRQ:     " << yn(registers.drq)
        << "  (check=" << yn(fdc->checkDRQActive()) << ")\n";

    oss << "  INTRQ:   " << yn(registers.intrq)
        << "  (check=" << yn(fdc->checkIRQActive()) << ")\n";

    oss << "  Sector Size: $" << hex4(registers.currentSectorSize)
        << " (" << std::dec << registers.currentSectorSize << " bytes)\n";

    oss << "  Data Index:  $" << hex2(registers.dataIndex)
        << " (" << std::dec << static_cast<unsigned>(registers.dataIndex) << ")\n";

    oss << "  In-Progress: read=" << yn(registers.readSectorInProgress)
        << " write=" << yn(registers.writeSectorInProgress)
        << " cyclesUntilEvent=" << registers.cyclesUntilEvent << "\n";

    std::cout << oss.str();
}

std::string MLMonitorBackend::jamModeToString() const
{
    if (processor)
    {
        CPU::JamMode mode = processor->getJamMode();
        switch(mode)
        {
            case CPU::JamMode::FreezePC: return "FreezePC";
            case CPU::JamMode::Halt: return "Halt";
            case CPU::JamMode::NopCompat: return "NopCompat";
        }
    }

    // Default
        return "Unknown";
}

std::string MLMonitorBackend::decodeDriveStatus(Drive::DriveStatus status)
{
    switch(status)
    {
        case Drive::DriveStatus::IDLE:      return "IDLE";
        case Drive::DriveStatus::READY:     return "READY";
        case Drive::DriveStatus::READING:   return "READING";
        case Drive::DriveStatus::WRITING:   return "WRITING";
        case Drive::DriveStatus::SEEKING:   return "SEEKING";
        default: return "IDLE";
     }

     return "IDLE";
}
