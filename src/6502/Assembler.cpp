// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "6502/Assembler.h"

Assembler::Assembler() = default;

Assembler::~Assembler() = default;

Assembler::AssembledInstruction Assembler::assembleLine(const std::string& line, uint16_t address)
{
    std::istringstream iss(line);
    std::string mnemonic, operandStr;
    iss >> mnemonic;
    std::getline(iss, operandStr);

    // Normalize to uppercase
    std::transform(mnemonic.begin(), mnemonic.end(), mnemonic.begin(), ::toupper);

    uint16_t value = 0;
    AddressingMode mode;

    try
    {
        mode = parseOperand(operandStr, value);
    }
    catch (const std::exception& e)
    {
        throw std::runtime_error("Error parsing line '" + line + "': " + e.what());
    }

    // Detect branch mnemonics
    if (mode == AddressingMode::Absolute &&
        (mnemonic == "BEQ" || mnemonic == "BNE" || mnemonic == "BMI" ||
        mnemonic == "BPL" || mnemonic == "BCS" || mnemonic == "BCC" ||
        mnemonic == "BVS" || mnemonic == "BVC"))
    {
        int16_t offset = static_cast<int16_t>(value) - static_cast<int16_t>(address + 2);
        if (offset < -128 || offset > 127)
        {
            throw std::runtime_error("Branch target out of range: " + operandStr);
        }
        value = static_cast<uint8_t>(offset & 0xFF);
        mode = AddressingMode::Relative;
    }

    uint8_t opcode = lookupOpcode(mnemonic, mode);

    std::vector<uint8_t> bytes;
    bytes.push_back(opcode);

    const InstructionInfo& info = OPCODES[opcode];
    if (info.length > 1) bytes.push_back(value & 0xFF);
    if (info.length > 2) bytes.push_back((value >> 8) & 0xFF);

    AssembledInstruction result;
    result.startAddress = address;
    result.nextAddress  = address + info.length;
    result.opcode       = opcode;
    result.bytes        = std::move(bytes);

    return result;
}

AddressingMode Assembler::parseOperand(const std::string& operand, uint16_t& value)
{
    std::string op = operand;
    op.erase(remove_if(op.begin(), op.end(), ::isspace), op.end());

    if (op.empty()) return AddressingMode::Implied;

    if (op[0] == '#')
    {
        value = std::stoi(op.substr(2), nullptr, 16);
        return AddressingMode::Immediate;
    }
    if (op.front() == '(')
    {
        if (op.size() > 2 && op.substr(op.size() - 3) == ",X)")
        {
            std::string inner = op.substr(2, op.size() - 5); // skip "( $" and remove ",X)"
            value = std::stoi(inner.substr(1), nullptr, 16); // drop leading $
            return AddressingMode::IndirectX;
        }
        else if (op.size() > 2 && op.substr(op.size() - 3) == "),Y")
        {
            std::string inner = op.substr(2, op.size() - 5); // skip "( $" and remove "),Y"
            value = std::stoi(inner.substr(1), nullptr, 16); // drop leading $
            return AddressingMode::IndirectY;
        }
        else
        {
            std::string inner = op.substr(1, op.size() - 2); // strip ( )
            value = std::stoi(inner.substr(1), nullptr, 16); // drop leading $
            if (value <= 0xFF) throw std::runtime_error("Invalid indirect operand: JMP ($" + inner + ")");
            return AddressingMode::Indirect;
        }
    }
    // Accumulator mode (ASL A, ROR A, etc.)
    if (op == "A" || op == "a")
    {
        return AddressingMode::Accumulator;
    }
    if (op.size() > 2 && op.substr(op.size() - 2) == ",X")
    {
        std::string base = op.substr(0, op.size() - 2); // strip off ",X"
        value = std::stoi(base.substr(1), nullptr, 16); // drop leading $
        return (value <= 0xFF) ? AddressingMode::ZeroPageX : AddressingMode::AbsoluteX;
    }
    if (op.size() > 2 && op.substr(op.size() - 2) == ",Y")
    {
        std::string base = op.substr(0, op.size() -2); // strip off ",Y"
        value = std::stoi(base.substr(1), nullptr, 16); // drop leading $
        return (value <= 0xFF) ? AddressingMode::ZeroPageY : AddressingMode::AbsoluteY;
    }
    if (op.front() == '$')
    {
        value = std::stoi(op.substr(1), nullptr, 16);
        return value <= 0xFF ? AddressingMode::ZeroPage : AddressingMode::Absolute;
    }

    throw std::runtime_error("Unsupported operand format: " + op);
}

uint8_t Assembler::lookupOpcode(const std::string& mnemonic, AddressingMode mode)
{
    MnemonicKey key{mnemonic, mode};
    auto it = MNEMONIC_TO_OPCODE.find(key);
    if (it == MNEMONIC_TO_OPCODE.end())
    {
        throw std::runtime_error("Unsupported mnemonic/mode: " + mnemonic + " (" + std::to_string(static_cast<int>(mode)) + ")");
    }
    return it->second;
}

