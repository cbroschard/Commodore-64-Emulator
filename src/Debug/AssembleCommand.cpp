// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Computer.h"
#include "Debug/AssembleCommand.h"
#include "Debug/MLMonitor.h"
#include "Debug/MLMonitorBackend.h"

AssembleCommand::AssembleCommand() = default;

AssembleCommand::~AssembleCommand() = default;

int AssembleCommand::order() const
{
    return 3;
}

std::string AssembleCommand::name() const
{
    return "a";
}

std::string AssembleCommand::category() const
{
    return "Debugging";
}


std::string AssembleCommand::shortHelp() const
{
     return "a         - Assemble mnemonic and operand into memory";
}

std::string AssembleCommand::help() const
{
    return R"(a <address> <mnemonic> [operand]

 Usage:
    a $C000 LDA #$01
    a $0800 JMP $1000
    a $2000 NOP

 Description:
    Assembles a 6502 instruction and writes the resulting opcode and
    operand bytes into memory at the given address.

 Arguments:
    <address>   The memory location where the instruction will be written.
    <mnemonic>  The 6502 instruction mnemonic (e.g., LDA, STA, JMP).
    [operand]   Optional operand for the instruction. Accepts immediate
                values (#$nn), zero page addresses ($nn), absolute
                addresses ($nnnn), indirect syntax ((...)), and indexed
                forms (,X or ,Y).

 Notes:
    - The assembler determines the correct addressing mode based on
      the operand syntax.
    - After assembly, memory is updated immediately and execution can
      continue or be inspected with the 'm' command.
    - This command is for one-line assembly. For larger code blocks,
      you can enter multiple 'a' commands or load a PRG/CRT file.)";
}

void AssembleCommand::execute(MLMonitor& mon, const std::vector<std::string>& args)
{
    // help or no-arg -> show usage
    if (args.size() == 1 || (args.size() > 1 && isHelp(args[1])))
    {
        std::cout << help() << std::endl;
        return;
    }

    uint16_t address = parseAddress(args[1]);

    Assembler assembler;
    std::string line;

    while (true) {
        // Prompt
        std::cout << std::hex << std::uppercase
                  << std::setw(4) << std::setfill('0')
                  << address << " > ";

        // Read user input
        std::getline(std::cin, line);
        if (line.empty()) break;  // exit on blank line

        try {
            auto instr = assembler.assembleLine(line, address);

            // Write into memory
            for (size_t i = 0; i < instr.bytes.size(); ++i) {
                mon.mlmonitorbackend()->writeRAM(address + i, instr.bytes[i]);
            }

            address = instr.nextAddress;  // advance
        }
        catch (const std::exception& e) {
            std::cout << "Assembly error: " << e.what() << "\n";
        }
    }
}

