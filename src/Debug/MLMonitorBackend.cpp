// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Drive/FDC177x.h"
#include "IECBUS.h"
#include "MLMonitorBackend.h"
#include "Peripheral.h"

static const char* cpuBusCycleTypeName(CPU::CpuBusCycleType type)
{
    switch (type)
    {
        case CPU::CpuBusCycleType::None:        return "None";
        case CPU::CpuBusCycleType::OpcodeFetch: return "OpcodeFetch";
        case CPU::CpuBusCycleType::Read:        return "Read";
        case CPU::CpuBusCycleType::Write:       return "Write";
        case CPU::CpuBusCycleType::DummyRead:   return "DummyRead";
        case CPU::CpuBusCycleType::DummyWrite:  return "DummyWrite";
        case CPU::CpuBusCycleType::StackRead:   return "StackRead";
        case CPU::CpuBusCycleType::StackWrite:  return "StackWrite";
        default:                                return "Unknown";
    }
}

MLMonitorBackend::MLMonitorBackend() :
    cart(nullptr),
    cass(nullptr),
    cia1(nullptr),
    cia2(nullptr),
    cpu(nullptr),
    bus(nullptr),
    logger(nullptr),
    pla(nullptr),
    sid(nullptr),
    vic(nullptr)
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
    while(vic->getCurrentRaster() != targetRaster)
    {
        vic->tick(1);
        cpu->tick();
        cia1->updateTimers(1);
        cia2->updateTimers(1);
        sid->tick(1);
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

void MLMonitorBackend::irqForceOn()
{
    if (irq)
        irq->raiseIRQ(IRQLine::MONITOR);
}

void MLMonitorBackend::irqForceOff()
{
    if (irq)
        irq->clearIRQ(IRQLine::MONITOR);
}

void MLMonitorBackend::irqDisableAll()
{
    if (!vic && !cia1 && !cia2) return;

    irqForceOff();

    snapshot.has = true;
    snapshot.vic  = vic->snapshotIRQs();
    snapshot.cia1 = cia1->snapshotIRQs();
    snapshot.cia2 = cia2->snapshotIRQs();

    vic->disableAllIRQs();
    cia1->disableAllIRQs();
    cia2->disableAllIRQs();

    irqClearAll();  // acknowledge anything pending after the mask change
}

void MLMonitorBackend::irqClearAll()
{
    if (!vic && !cia1 && !cia2) return;

    irqForceOff();

    vic->clearPendingIRQs();
    cia1->clearPendingIRQs();
    cia2->clearPendingIRQs();
}

void MLMonitorBackend::irqRestore()
{
    if (!vic && !cia1 && !cia2) return;
    if (!snapshot.has) return;

    irqForceOff();

    vic->restoreIRQs(snapshot.vic);
    cia1->restoreIRQs(snapshot.cia1);
    cia2->restoreIRQs(snapshot.cia2);
}

void MLMonitorBackend::setLogging(LogSet log, bool enabled)
{
    switch (log)
    {
        case LogSet::Cartridge: if (cart) cart->setLog(enabled); break;
        case LogSet::Cassette: if (cass) cass->setLog(enabled); break;
        case LogSet::CIA1: if (cia1) cia1->setLog(enabled); break;
        case LogSet::CIA2: if (cia2) cia2->setLog(enabled); break;
        case LogSet::CPU: if (cpu) cpu->setLog(enabled); break;
        case LogSet::IO: if (io) io->setLog(enabled); break;
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
        case LogSet::VIC: if (vic) vic->setLog(enabled); break;
    }
}

void MLMonitorBackend::setPC(uint16_t value)
{
    if (!cpu)
        return;

    cpu->setPC(value);
    cpu->forceInstructionBoundaryForMonitor();
}

void MLMonitorBackend::cpuStepInstruction()
{
    if (!cpu)
        return;

    // Start or continue the current instruction.
    cpu->tick();

    // Finish the instruction by consuming its remaining cycles.
    int guard = 128;

    while (!cpu->isAtInstructionBoundary() && guard-- > 0)
    {
        cpu->tick();
    }
}

std::string MLMonitorBackend::cpuAddressStatus() const
{
    if (!cpu)
        return "CPU not attached.\n";

    const auto s = cpu->getLastAddressDebugState();

    auto hexByte = [](uint8_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(2) << int(v);
        return os.str();
    };

    auto hexWord = [](uint16_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(4) << int(v);
        return os.str();
    };

    auto modeName = [](CPU::CPUAddressDebugMode mode)
    {
        switch (mode)
        {
            case CPU::CPUAddressDebugMode::IndirectX:         return "($nn,X)";
            case CPU::CPUAddressDebugMode::IndirectY:         return "($nn),Y";
            case CPU::CPUAddressDebugMode::IndirectYBoundary: return "($nn),Y read";
            default:                                          return "None";
        }
    };

    std::ostringstream out;

    out << "Last Addressing Mode\n";
    out << "--------------------\n";

    if (!s.valid)
    {
        out << "No indirect addressing mode has been recorded yet.\n";
        return out.str();
    }

    out << "Mode:           " << modeName(s.mode) << "\n";
    out << "Operand PC:     $" << hexWord(s.operandPC) << "\n";
    out << "ZP operand:     $" << hexByte(s.zpOperand) << "\n";
    out << "Index value:    $" << hexByte(s.indexValue) << "\n";
    out << "Indexed ZP:     $" << hexByte(s.indexedZP) << "\n";
    out << "Pointer low:    $" << hexByte(s.pointerLowAddr)
        << " -> $" << hexByte(s.pointerLowValue) << "\n";
    out << "Pointer high:   $" << hexByte(s.pointerHighAddr)
        << " -> $" << hexByte(s.pointerHighValue) << "\n";
    out << "Base address:   $" << hexWord(s.baseAddress) << "\n";
    out << "Effective addr: $" << hexWord(s.effectiveAddress) << "\n";
    out << "Page crossed:   " << (s.pageCrossed ? "yes" : "no") << "\n";

    if (s.dummyReadUsed)
        out << "Dummy read:     $" << hexWord(s.dummyReadAddress) << "\n";
    else
        out << "Dummy read:     none\n";

    if (s.valueRead != 0 || s.mode == CPU::CPUAddressDebugMode::IndirectYBoundary)
        out << "Value read:     $" << hexByte(s.valueRead) << "\n";

    out << "Total cycles:   " << std::dec << s.totalCycles << "\n";

    return out.str();
}

std::string MLMonitorBackend::cpuBranchStatus() const
{
    if (!cpu)
        return "CPU not attached.\n";

    const auto s = cpu->getLastBranchDebugState();

    auto hexByte = [](uint8_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(2) << int(v);
        return os.str();
    };

    auto hexWord = [](uint16_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(4) << int(v);
        return os.str();
    };

    std::ostringstream out;

    out << "Last Branch\n";
    out << "-----------\n";

    if (!s.valid)
    {
        out << "No branch has been recorded yet.\n";
        return out.str();
    }

    out << "Opcode PC:      $" << hexWord(s.opcodePC) << "\n";
    out << "Opcode:         $" << hexByte(s.opcode) << "\n";
    out << "Mnemonic:       " << (s.mnemonic ? s.mnemonic : "") << "\n";
    out << "Condition:      " << (s.condition ? "true" : "false") << "\n";
    out << "Taken:          " << (s.taken ? "yes" : "no") << "\n";
    out << "Offset:         " << std::dec << int(s.offset) << "\n";
    out << "Operand PC:     $" << hexWord(s.operandPC) << "\n";
    out << "Old PC:         $" << hexWord(s.oldPC) << "\n";
    out << "New PC:         $" << hexWord(s.newPC) << "\n";
    out << "Page crossed:   " << (s.pageCrossed ? "yes" : "no") << "\n";

    if (s.taken)
        out << "Taken dummy:    $" << hexWord(s.takenDummyRead) << "\n";
    else
        out << "Taken dummy:    none\n";

    if (s.pageCrossed)
        out << "Page dummy:     $" << hexWord(s.pageCrossDummyRead) << "\n";
    else
        out << "Page dummy:     none\n";

    out << "Extra cycles:   " << std::dec << int(s.extraCycles) << "\n";
    out << "Total cycles:   " << std::dec << s.totalCycles << "\n";

    return out.str();
}

std::string MLMonitorBackend::cpuInterruptStatus() const
{
    if (!cpu)
        return "CPU not attached.\n";

    const auto s = cpu->getLastInterruptEntryDebugState();

    auto hexByte = [](uint8_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(2) << int(v);
        return os.str();
    };

    auto hexWord = [](uint16_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(4) << int(v);
        return os.str();
    };

    auto typeName = [](CPU::InterruptEntryType type)
    {
        switch (type)
        {
            case CPU::InterruptEntryType::IRQ: return "IRQ";
            case CPU::InterruptEntryType::NMI: return "NMI";
            case CPU::InterruptEntryType::BRK: return "BRK";
            default: return "None";
        }
    };

    std::ostringstream out;

    out << "Last Interrupt Entry\n";
    out << "--------------------\n";
    out << "Type:          " << typeName(s.type) << "\n";

    if (s.type == CPU::InterruptEntryType::None)
    {
        out << "No interrupt entry has been recorded yet.\n";
        return out.str();
    }

    out << "Accepted PC:   $" << hexWord(s.acceptedAtPC) << "\n";
    out << "Pushed return: $" << hexWord(s.pushedReturnPC) << "\n";
    out << "Pushed SR:     $" << hexByte(s.pushedSR) << "\n";
    out << "Vector addr:   $" << hexWord(s.vectorAddress) << "\n";
    out << "Vector target: $" << hexWord(s.vectorTarget) << "\n";
    out << "Total cycles:  " << std::dec << s.totalCycles << "\n";

    out << "SP before:     $" << hexByte(s.spBefore) << "\n";
    out << "SP after:      $" << hexByte(s.spAfter) << "\n";

    return out.str();
}

std::string MLMonitorBackend::cpuIrqStatus() const
{
    if (!cpu)
        return "CPU not attached.\n";

    const auto s = cpu->getIrqDebugState();

    auto hexByte = [](uint8_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(2) << int(v);
        return os.str();
    };

    auto hexWord = [](uint16_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(4) << int(v);
        return os.str();
    };

    auto flagsString = [](uint8_t sr)
    {
        std::string f;
        f += (sr & 0x80) ? 'N' : '.';
        f += (sr & 0x40) ? 'V' : '.';
        f += '-';
        f += (sr & 0x10) ? 'B' : '.';
        f += (sr & 0x08) ? 'D' : '.';
        f += (sr & 0x04) ? 'I' : '.';
        f += (sr & 0x02) ? 'Z' : '.';
        f += (sr & 0x01) ? 'C' : '.';
        return f;
    };

    std::ostringstream out;

    out << "CPU IRQ/NMI State\n";
    out << "-----------------\n";

    out << "PC:              $" << hexWord(s.pc) << "\n";
    out << "SR:              $" << hexByte(s.sr)
        << "  " << flagsString(s.sr) << "\n";

    out << "I flag:          " << (s.iFlag ? "set" : "clear") << "\n";
    out << "IRQ line:        " << (s.irqLineActive ? "active" : "inactive") << "\n";
    out << "NMI line:        " << (s.nmiLine ? "high" : "low") << "\n";
    out << "NMI pending:     " << (s.nmiPending ? "yes" : "no") << "\n";
    out << "IRQ suppress:    " << (s.irqSuppressOne ? "yes" : "no") << "\n";
    out << "RDY/BA line:     " << (s.rdyLine ? "high / released" : "low / CPU wait") << "\n";
    out << "AEC line:        " << (s.aecLine ? "high / CPU bus enabled" : "low / VIC owns bus") << "\n";
    out << "SO level:        " << (s.soLevel ? "high" : "low") << "\n";
    out << "Cycles left:     " << s.cyclesRemaining << "\n";
    out << "Total cycles:    " << s.totalCycles << "\n";

    out << "\nLast instruction:\n";
    out << "PC:              $" << hexWord(s.lastOpcodePC) << "\n";
    out << "Opcode:          $" << hexByte(s.lastOpcode) << "\n";

    return out.str();
}

std::string MLMonitorBackend::cpuCycleStatus() const
{
    if (!cpu)
        return "CPU not attached.\n";

    auto hexByte = [](uint8_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(2) << int(v);
        return os.str();
    };

    auto hexWord = [](uint16_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(4) << int(v);
        return os.str();
    };

    const auto s = cpu->getCycleDebugState();

    std::ostringstream out;

    out << "CPU Cycle State\n";
    out << "---------------\n";

    out << "Total cycles:       " << s.totalCycles << "\n";
    out << "Cycles remaining:   " << s.cyclesRemaining << "\n";
    out << "Between instr:      " << (s.betweenInstructions ? "yes" : "no") << "\n";
    out << "RDY/BA line:        " << (s.rdyLine ? "high / released" : "low / CPU wait") << "\n";
    out << "AEC line:           " << (s.aecLine ? "high / CPU bus enabled" : "low / VIC owns bus") << "\n";
    out << "Halted/JAM:         " << (s.halted ? "yes" : "no") << "\n";

    out << "\nVideo timing:\n";
    out << "  Mode:             " << (s.mode == VideoMode::PAL ? "PAL" : "NTSC") << "\n";
    out << "  Frame cycles:     " << s.frameCycle << " / " << s.cyclesPerFrame << "\n";
    out << "  Raster/Dot:       " << s.raster << " / " << s.dot << "\n";

    out << "\nLast instruction:\n";
    out << "PC:                 $" << hexWord(s.lastOpcodePC) << "\n";
    out << "Opcode:             $" << hexByte(s.lastOpcode) << "\n";

    out << "\nBus cycle:\n";
    out << "  Active:           " << (s.busCycleActive ? "yes" : "no") << "\n";
    out << "  Type:             " << cpuBusCycleTypeName(s.busCycleType) << "\n";
    out << "  Address:          $" << hexWord(s.busAddress) << "\n";
    out << "  Value:            $" << hexByte(s.busValue) << "\n";

    return out.str();
}

std::string MLMonitorBackend::cpuJMPStatus() const
{
    if (!cpu)
        return "CPU not attached.\n";

    const auto s = cpu->getLastJMPDebugState();

    auto hexByte = [](uint8_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(2) << int(v);
        return os.str();
    };

    auto hexWord = [](uint16_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(4) << int(v);
        return os.str();
    };

    std::ostringstream out;

    out << "Last JMP\n";
    out << "--------\n";

    if (!s.valid)
    {
        out << "No JMP has been recorded yet.\n";
        return out.str();
    }

    out << "JMP opcode PC:  $" << hexWord(s.jmpOpcodePC) << "\n";
    out << "Opcode:         $" << hexByte(s.opcode) << "\n";
    out << "Mode:           " << (s.indirect ? "indirect" : "absolute") << "\n";
    out << "Operand addr:   $" << hexWord(s.operandAddress) << "\n";

    if (s.indirect)
    {
        out << "Pointer addr:   $" << hexWord(s.pointerAddress) << "\n";
        out << "Low read addr:  $" << hexWord(s.lowReadAddress) << "\n";
        out << "High read addr: $" << hexWord(s.highReadAddress) << "\n";
        out << "Low/high bytes: $" << hexByte(s.lowByte)
            << " / $" << hexByte(s.highByte) << "\n";
        out << "Page bug:       " << (s.indirectPageBug ? "yes" : "no") << "\n";
    }
    else
    {
        out << "Target low/high:$" << hexByte(s.lowByte)
            << " / $" << hexByte(s.highByte) << "\n";
    }

    out << "Final PC:       $" << hexWord(s.finalPC) << "\n";
    out << "Total cycles:   " << std::dec << s.totalCycles << "\n";

    return out.str();
}

std::string MLMonitorBackend::cpuJSRStatus() const
{
    if (!cpu)
        return "CPU not attached.\n";

    const auto s = cpu->getLastJSRDebugState();

    auto hexByte = [](uint8_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(2) << int(v);
        return os.str();
    };

    auto hexWord = [](uint16_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(4) << int(v);
        return os.str();
    };

    std::ostringstream out;

    out << "Last JSR Stack Push\n";
    out << "-------------------\n";

    if (!s.valid)
    {
        out << "No JSR has been recorded yet.\n";
        return out.str();
    }

    out << "JSR opcode PC:  $" << hexWord(s.jsrOpcodePC) << "\n";
    out << "Target PC:      $" << hexWord(s.targetPC) << "\n";
    out << "Pushed return:  $" << hexWord(s.pushedReturn) << "\n";
    out << "Pushed high/low:$" << hexByte(s.pushedHigh)
        << " / $" << hexByte(s.pushedLow) << "\n";
    out << "SP before:      $" << hexByte(s.spBefore) << "\n";
    out << "SP after:       $" << hexByte(s.spAfter) << "\n";
    out << "Total cycles:   " << std::dec << s.totalCycles << "\n";

    return out.str();
}

std::string MLMonitorBackend::cpuPHAStatus() const
{
    if (!cpu)
        return "CPU not attached.\n";

    const auto s = cpu->getLastPHADebugState();

    auto hexByte = [](uint8_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(2) << int(v);
        return os.str();
    };

    auto hexWord = [](uint16_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(4) << int(v);
        return os.str();
    };

    std::ostringstream out;

    out << "Last PHA Stack Push\n";
    out << "-------------------\n";

    if (!s.valid)
    {
        out << "No PHA has been recorded yet.\n";
        return out.str();
    }

    out << "PHA opcode PC:  $" << hexWord(s.phaOpcodePC) << "\n";
    out << "Pushed A:       $" << hexByte(s.pushedA) << "\n";
    out << "SP before:      $" << hexByte(s.spBefore) << "\n";
    out << "SP after:       $" << hexByte(s.spAfter) << "\n";
    out << "Total cycles:   " << std::dec << s.totalCycles << "\n";

    return out.str();
}

std::string MLMonitorBackend::cpuPHPStatus() const
{
    if (!cpu)
        return "CPU not attached.\n";

    const auto s = cpu->getLastPHPDebugState();

    auto hexByte = [](uint8_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(2) << int(v);
        return os.str();
    };

    auto hexWord = [](uint16_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(4) << int(v);
        return os.str();
    };

    std::ostringstream out;

    out << "Last PHP Status Push\n";
    out << "--------------------\n";

    if (!s.valid)
    {
        out << "No PHP has been recorded yet.\n";
        return out.str();
    }

    out << "PHP opcode PC:  $" << hexWord(s.phpOpcodePC) << "\n";
    out << "Internal SR:    $" << hexByte(s.internalSR) << "\n";
    out << "Pushed SR:      $" << hexByte(s.pushedSR) << "\n";
    out << "SP before:      $" << hexByte(s.spBefore) << "\n";
    out << "SP after:       $" << hexByte(s.spAfter) << "\n";
    out << "Total cycles:   " << std::dec << s.totalCycles << "\n";

    return out.str();
}

std::string MLMonitorBackend::cpuPLAStatus() const
{
    if (!cpu)
        return "CPU not attached.\n";

    const auto s = cpu->getLastPLADebugState();

    auto hexByte = [](uint8_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(2) << int(v);
        return os.str();
    };

    auto hexWord = [](uint16_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(4) << int(v);
        return os.str();
    };

    std::ostringstream out;

    out << "Last PLA Stack Pull\n";
    out << "-------------------\n";

    if (!s.valid)
    {
        out << "No PLA has been recorded yet.\n";
        return out.str();
    }

    out << "PLA opcode PC:  $" << hexWord(s.plaOpcodePC) << "\n";
    out << "Pulled A:       $" << hexByte(s.pulledA) << "\n";
    out << "Final A:        $" << hexByte(s.finalA) << "\n";
    out << "Z flag:         " << (s.zFlag ? "set" : "clear") << "\n";
    out << "N flag:         " << (s.nFlag ? "set" : "clear") << "\n";
    out << "SP before:      $" << hexByte(s.spBefore) << "\n";
    out << "SP after:       $" << hexByte(s.spAfter) << "\n";
    out << "Total cycles:   " << std::dec << s.totalCycles << "\n";

    return out.str();
}

std::string MLMonitorBackend::cpuPLPStatus() const
{
    if (!cpu)
        return "CPU not attached.\n";

    const auto s = cpu->getLastPLPDebugState();

    auto hexByte = [](uint8_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(2) << int(v);
        return os.str();
    };

    auto hexWord = [](uint16_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(4) << int(v);
        return os.str();
    };

    std::ostringstream out;

    out << "Last PLP Status Restore\n";
    out << "-----------------------\n";

    if (!s.valid)
    {
        out << "No PLP has been recorded yet.\n";
        return out.str();
    }

    out << "PLP opcode PC:  $" << hexWord(s.plpOpcodePC) << "\n";
    out << "Pulled SR:      $" << hexByte(s.pulledSR) << "\n";
    out << "Final SR:       $" << hexByte(s.finalSR) << "\n";
    out << "SP before:      $" << hexByte(s.spBefore) << "\n";
    out << "SP after:       $" << hexByte(s.spAfter) << "\n";
    out << "I old/new:      " << (s.oldI ? "1" : "0")
        << " -> " << (s.newI ? "1" : "0") << "\n";
    out << "IRQ suppress:   " << (s.irqSuppressSet ? "set" : "not set") << "\n";
    out << "Total cycles:   " << std::dec << s.totalCycles << "\n";

    return out.str();
}

std::string MLMonitorBackend::cpuRTIStatus() const
{
    if (!cpu)
        return "CPU not attached.\n";

    const auto rti = cpu->getLastRTIDebugState();
    const auto intr = cpu->getLastInterruptEntryDebugState();

    auto hexByte = [](uint8_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(2) << int(v);
        return os.str();
    };

    auto hexWord = [](uint16_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(4) << int(v);
        return os.str();
    };

    std::ostringstream out;

    out << "Last RTI Return\n";
    out << "---------------\n";

    if (!rti.valid)
    {
        out << "No RTI has been recorded yet.\n";

        if (intr.type != CPU::InterruptEntryType::None)
            out << "Compared to intr: no RTI recorded after last interrupt entry\n";

        return out.str();
    }

    out << "RTI opcode PC:  $" << hexWord(rti.rtiOpcodePC) << "\n";
    out << "Pulled SR:      $" << hexByte(rti.pulledSR) << "\n";
    out << "Final SR:       $" << hexByte(rti.finalSR) << "\n";
    out << "Pulled PCL/PCH: $" << hexByte(rti.pulledPCL)
        << " / $" << hexByte(rti.pulledPCH) << "\n";
    out << "Return PC:      $" << hexWord(rti.returnPC) << "\n";
    out << "SP before:      $" << hexByte(rti.spBefore) << "\n";
    out << "SP after:       $" << hexByte(rti.spAfter) << "\n";
    out << "I old/new:      " << (rti.oldI ? "1" : "0")
        << " -> " << (rti.newI ? "1" : "0") << "\n";
    out << "IRQ suppress:   " << (rti.irqSuppressSet ? "set" : "not set") << "\n";
    out << "Total cycles:   " << std::dec << rti.totalCycles << "\n";

    if (intr.type != CPU::InterruptEntryType::None)
    {
        out << "Compared to intr: ";

        if (rti.totalCycles < intr.totalCycles)
        {
            out << "STALE - RTI occurred before last interrupt entry\n";
        }
        else if (rti.totalCycles == intr.totalCycles)
        {
            out << "same cycle as last interrupt entry\n";
        }
        else
        {
            out << "fresh - RTI occurred after last interrupt entry\n";
        }
    }

    return out.str();
}

std::string MLMonitorBackend::cpuRTSStatus() const
{
    if (!cpu)
        return "CPU not attached.\n";

    const auto s = cpu->getLastRTSDebugState();

    auto hexByte = [](uint8_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(2) << int(v);
        return os.str();
    };

    auto hexWord = [](uint16_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(4) << int(v);
        return os.str();
    };

    std::ostringstream out;

    out << "Last RTS Stack Pull\n";
    out << "-------------------\n";

    if (!s.valid)
    {
        out << "No RTS has been recorded yet.\n";
        return out.str();
    }

    out << "RTS opcode PC:  $" << hexWord(s.rtsOpcodePC) << "\n";
    out << "Pulled return:  $" << hexWord(s.pulledReturn) << "\n";
    out << "Final PC:       $" << hexWord(s.finalPC) << "\n";
    out << "Pulled low/high:$" << hexByte(s.pulledLow)
        << " / $" << hexByte(s.pulledHigh) << "\n";
    out << "SP before:      $" << hexByte(s.spBefore) << "\n";
    out << "SP after:       $" << hexByte(s.spAfter) << "\n";
    out << "Total cycles:   " << std::dec << s.totalCycles << "\n";

    return out.str();
}

std::string MLMonitorBackend::cpuStackStatus(int count) const
{
    if (!cpu)
        return "CPU not attached.\n";

    if (count <= 0)
        count = 16;

    if (count > 256)
        count = 256;

    const uint8_t sp = cpu->getSP();
    const uint8_t first = uint8_t(sp + 1);

    auto hexByte = [](uint8_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(2) << int(v);
        return os.str();
    };

    auto hexWord = [](uint16_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(4) << int(v);
        return os.str();
    };

    std::ostringstream out;

    out << "CPU Stack\n";
    out << "---------\n";
    out << "SP:              $" << hexByte(sp) << "\n";
    out << "Next pop addr:   $" << hexWord(uint16_t(0x0100 | first)) << "\n";
    out << "Count:           " << std::dec << count << "\n\n";

    out << "Pop#  Addr   Value\n";

    for (int i = 0; i < count; ++i)
    {
        const uint8_t stackOffset = uint8_t(first + i);
        const uint16_t addr = uint16_t(0x0100 | stackOffset);
        const uint8_t value = cpu->debugRead(addr);

        out << std::dec << std::setw(4) << i << "  "
            << "$" << hexWord(addr) << "  "
            << "$" << hexByte(value) << "\n";
    }

    return out.str();
}

std::string MLMonitorBackend::cpuLastStatus() const
{
    if (!cpu)
        return "CPU not attached.\n";

    const auto s = cpu->getCycleDebugState();

    auto hexByte = [](uint8_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(2) << int(v);
        return os.str();
    };

    auto hexWord = [](uint16_t v)
    {
        std::ostringstream os;
        os << std::uppercase << std::hex << std::setfill('0')
           << std::setw(4) << int(v);
        return os.str();
    };

    std::ostringstream out;

    out << "Last CPU Instruction\n";
    out << "--------------------\n";
    out << "PC:          $" << hexWord(s.lastOpcodePC) << "\n";
    out << "Opcode:      $" << hexByte(s.lastOpcode) << "\n";
    out << "Cycles left: " << s.cyclesRemaining << "\n";
    out << "Raster/Dot:  " << s.raster << " / " << s.dot << "\n";

    return out.str();
}

void MLMonitorBackend::setJamMode(const std::string& mode)
{
    if (cpu)
    {
        if (mode == "freeze")
        {
            cpu->setJamMode(CPU::JamMode::FreezePC);
        }
        else if (mode == "halt")
        {
            cpu->setJamMode(CPU::JamMode::Halt);
        }
        else if (mode == "nop")
        {
            cpu->setJamMode(CPU::JamMode::NopCompat);
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

    std::cout << "Drive #" << id << " IEC monitor state:\n\n";

    std::cout << "Drive IEC physical:\n";
    std::cout << "  Lines seen:        "
              << "ATN="  << HL(s.atnLow)  << "  "
              << "CLK="  << HL(s.clkLow)  << "  "
              << "DATA=" << HL(s.dataLow) << "  "
              << "SRQ="  << HL(s.srqLow)  << "\n";

    auto yesno = [](bool v) { return v ? "yes" : "no"; };

    std::cout << "  Drive pulls low:   "
          << "ATN="  << yesno(s.drvAssertAtn)  << "  "
          << "CLK="  << yesno(s.drvAssertClk)  << "  "
          << "DATA=" << yesno(s.drvAssertData) << "  "
          << "SRQ="  << yesno(s.drvAssertSrq)  << "\n\n";

    std::cout << "Legacy/software IEC decode:\n";
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
    if (cpu)
    {
        CPU::JamMode mode = cpu->getJamMode();
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
