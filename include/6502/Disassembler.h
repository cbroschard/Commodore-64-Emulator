// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef DISASSEMBLER_H
#define DISASSEMBLER_H

#include <iomanip>
#include <string>
#include <sstream>
#include "6502/Opcode6502.h"
#include "CPUBus.h"
#include "Memory.h"

class Disassembler
{
    public:
        Disassembler();
        virtual ~Disassembler();

        // Disassemble a single instruction
        static std::string disassembleAt(uint16_t pc, Memory& mem);
        static std::string disassembleAt(uint16_t pc, CPUBus& bus);

        // Disassemble given range of addresses (start, end)
        static std::string disassembleRange(uint16_t start, uint16_t end, uint16_t& lastPC, Memory& mem);

    protected:

    private:
        // Helpers
        static std::string formatOperand(const InstructionInfo& info, uint16_t pc, Memory& mem);
        static std::string formatOperand(const InstructionInfo& info, uint16_t pc, CPUBus& bus);
        static std::string hexByte(uint8_t value);
        static std::string hexWord(uint16_t value);
};

#endif // DISASSEMBLER_H
