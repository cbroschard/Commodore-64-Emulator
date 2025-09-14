// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef ASSEMBLER_H
#define ASSEMBLER_H

#include <algorithm>
#include <string>
#include <vector>
#include "Debug/CommandUtils.h"
#include "6502/Opcode6502.h"

class Assembler
{
    public:
        Assembler();
        virtual ~Assembler();

        struct AssembledInstruction
        {
            uint16_t startAddress;          // where it was placed
            uint16_t nextAddress;           // where the next instruction should go
            uint8_t opcode;                 // just for convenience
            std::vector<uint8_t> bytes;     // encoded instruction
        };

        // Assemble one line into bytes
        AssembledInstruction assembleLine(const std::string& line, uint16_t address);

    protected:

    private:

        AddressingMode parseOperand(const std::string& operand, uint16_t& value);
        uint8_t lookupOpcode(const std::string& mnemonic, AddressingMode mode);

};

#endif // ASSEMBLER_H
