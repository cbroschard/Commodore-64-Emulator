// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef OPCODE6502_H_INCLUDED
#define OPCODE6502_H_INCLUDED

#include <cstdint>
#include <unordered_map>
#include <string>

enum class AddressingMode
{
    Implied, Immediate, ZeroPage, ZeroPageX, ZeroPageY,
    Absolute, AbsoluteX, AbsoluteY, Indirect, IndirectX, IndirectY,
    Relative, Accumulator
};

struct InstructionInfo
{
    const char* mnemonic;
    AddressingMode mode;
    int length;     // instruction size in bytes
    uint8_t opcode; // actual opcode byte (0–255)
};

// Declaration of master table
extern const InstructionInfo OPCODES[256];

// Reverse lookup key
struct MnemonicKey {
    std::string mnemonic;
    AddressingMode mode;

    bool operator==(const MnemonicKey& other) const {
        return mnemonic == other.mnemonic && mode == other.mode;
    }
};

struct MnemonicKeyHash {
    std::size_t operator()(const MnemonicKey& k) const {
        return std::hash<std::string>()(k.mnemonic) ^ (std::hash<int>()((int)k.mode) << 1);
    }
};

// Reverse lookup for assembler
extern const std::unordered_map<MnemonicKey, uint8_t, MnemonicKeyHash> MNEMONIC_TO_OPCODE;

#endif // OPCODE6502_H_INCLUDED
