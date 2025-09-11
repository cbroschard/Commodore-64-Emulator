// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Disassembler.h"

Disassembler::Disassembler() = default;

Disassembler::~Disassembler() = default;

std::string Disassembler::disassembleAt(uint16_t pc, Memory& mem)
{
    uint8_t opcode = mem.read(pc);
    const InstructionInfo& info = OPCODES[opcode];

    std::ostringstream out;

    // Address
    out << hexWord(pc) << "  ";

    // Raw bytes
    for (int i = 0; i < info.length; i++) {
        out << hexByte(mem.read(pc + i)) << " ";
    }
    for (int i = info.length; i < 3; i++) {
        out << "   "; // padding
    }

    // Mnemonic
    out << " " << info.mnemonic << " ";

    // Operand
    out << formatOperand(info, pc, mem);

    return out.str();
}

std::string Disassembler::disassembleRange(uint16_t start, uint16_t end, uint16_t& lastPC, Memory& mem)
{
    std::ostringstream out;
    uint16_t pc = start;

    while (pc <= end) {
        uint8_t opcode = mem.read(pc);
        const InstructionInfo& info = OPCODES[opcode];

        // Disassemble current instruction
        out << disassembleAt(pc, mem) << "\n";

        // Advance PC by instruction length
        pc += info.length;
    }

    lastPC = pc;
    return out.str();
}

std::string Disassembler::formatOperand(const InstructionInfo& info, uint16_t pc, Memory& mem)
{
    std::ostringstream out;

    switch (info.mode) {
        case AddressingMode::Immediate:
            out << "#$" << hexByte(mem.read(pc + 1));
            break;
        case AddressingMode::ZeroPage:
            out << "$" << hexByte(mem.read(pc + 1));
            break;
        case AddressingMode::ZeroPageX:
            out << "$" << hexByte(mem.read(pc + 1)) << ",X";
            break;
        case AddressingMode::ZeroPageY:
            out << "$" << hexByte(mem.read(pc + 1)) << ",Y";
            break;
        case AddressingMode::Absolute:
            out << "$" << hexWord(mem.read(pc + 1) | (mem.read(pc + 2) << 8));
            break;
        case AddressingMode::AbsoluteX:
            out << "$" << hexWord(mem.read(pc + 1) | (mem.read(pc + 2) << 8)) << ",X";
            break;
        case AddressingMode::AbsoluteY:
            out << "$" << hexWord(mem.read(pc + 1) | (mem.read(pc + 2) << 8)) << ",Y";
            break;
        case AddressingMode::Indirect:
            out << "($" << hexWord(mem.read(pc + 1) | (mem.read(pc + 2) << 8)) << ")";
            break;
        case AddressingMode::IndirectX:
            out << "($" << hexByte(mem.read(pc + 1)) << ",X)";
            break;
        case AddressingMode::IndirectY:
            out << "($" << hexByte(mem.read(pc + 1)) << "),Y";
            break;
        case AddressingMode::Relative: {
            int8_t offset = static_cast<int8_t>(mem.read(pc + 1));
            uint16_t target = pc + 2 + offset;
            out << "$" << hexWord(target);
            break;
        }
        case AddressingMode::Accumulator:
            out << "A";
            break;
        case AddressingMode::Implied:
            // no operand
            break;
        default:
            out << "???";
            break;
    }

    return out.str();
}

std::string Disassembler::hexByte(uint8_t value)
{
    std::ostringstream ss;
    ss << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << (int)value;
    return ss.str();
}

std::string Disassembler::hexWord(uint16_t value)
{
    std::ostringstream ss;
    ss << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << value;
    return ss.str();
}
