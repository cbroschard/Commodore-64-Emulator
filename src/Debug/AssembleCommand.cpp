// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include <iomanip>
#include <iostream>
#include "Debug/AssembleCommand.h"
#include "Debug/MLMonitor.h"
#include "Debug/MLMonitorBackend.h"

AssembleCommand::AssembleCommand() :
    interactiveActive(false),
    interactiveAddress(0)
{

}

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
    return "a <addr> [mnemonic] - Assemble 6502 instruction(s) into memory";
}

std::string AssembleCommand::help() const
{
    return R"(a - Assemble 6502 instructions into memory

Usage:
    a <address>
    a <address> <mnemonic> [operand]

Modes:
    a <address>
        Starts interactive assembly mode at the given address.
        Enter one instruction per line.
        Press Enter on a blank line, or enter '.', to end assembly.

    a <address> <mnemonic> [operand]
        Assembles one instruction and writes it directly to memory.

Examples:
    a $C000
    a $C000 LDA #$01
    a $0800 JMP $1000
    a $2000 NOP
    a $C000 STA $D020
    a $C000 LDA ($FB),Y

Description:
    Assembles a 6502 instruction and writes the resulting opcode and
    operand bytes into memory at the given address.

Arguments:
    <address>   The memory location where the instruction will be written.
    <mnemonic>  The 6502 instruction mnemonic, such as LDA, STA, JMP, or NOP.
    [operand]   Optional operand for the instruction. Accepts immediate
                values (#$nn), zero page addresses ($nn), absolute
                addresses ($nnnn), indirect syntax ((...)), and indexed
                forms (,X or ,Y).

Notes:
    - The assembler determines the addressing mode from the operand syntax.
    - Memory is updated immediately after each assembled instruction.
    - In interactive mode, the prompt advances to the next instruction address.
    - Use the 'm' command to inspect the bytes after assembly.
)";
}

bool AssembleCommand::isInteractiveActive() const
{
    return interactiveActive;
}

bool AssembleCommand::assembleAndWrite(MLMonitor& mon,
                                       uint16_t address,
                                       const std::string& line,
                                       uint16_t& nextAddress)
{
    Assembler assembler;
    auto instr = assembler.assembleLine(line, address);

    for (size_t i = 0; i < instr.bytes.size(); ++i)
    {
        mon.mlmonitorbackend()->writeRAM(
            static_cast<uint16_t>(address + i),
            instr.bytes[i]
        );
    }

    std::cout << std::uppercase << std::hex
              << std::setw(4) << std::setfill('0')
              << static_cast<int>(address)
              << ": ";

    for (uint8_t b : instr.bytes)
    {
        std::cout << std::setw(2) << std::setfill('0')
                  << static_cast<int>(b)
                  << ' ';
    }

    std::cout << "   " << line << "\n"
              << std::dec << std::setfill(' ');

    nextAddress = instr.nextAddress;
    return true;
}

bool AssembleCommand::handleInteractiveLine(MLMonitor& mon, const std::string& line)
{
    if (!interactiveActive)
        return false;

    const std::string trimmed = trimCopy(line);

    if (trimmed.empty() || trimmed == ".")
    {
        interactiveActive = false;
        std::cout << "Assembly ended.\n";
        return true;
    }

    try
    {
        uint16_t next = interactiveAddress;
        assembleAndWrite(mon, interactiveAddress, trimmed, next);
        interactiveAddress = next;
    }
    catch (const std::exception& e)
    {
        std::cout << "Assembly error: " << e.what() << "\n";
    }

    return true;
}

std::string AssembleCommand::currentPrompt() const
{
    std::ostringstream oss;

    oss << std::uppercase << std::hex
        << std::setw(4) << std::setfill('0')
        << static_cast<int>(interactiveAddress)
        << " > ";

    return oss.str();
}

void AssembleCommand::execute(MLMonitor& mon, const std::vector<std::string>& args)
{
    if (args.size() == 1 || (args.size() > 1 && isHelp(args[1])))
    {
        std::cout << help() << std::endl;
        return;
    }

    uint16_t address = 0;

    try
    {
        address = parseAddress(args[1]);
    }
    catch (const std::exception& e)
    {
        std::cout << "Invalid address: " << args[1] << "\n";
        std::cout << "Usage: a <address> [mnemonic] [operand]\n";
        return;
    }

    MLMonitorBackend* backend = mon.mlmonitorbackend();

    if (backend == nullptr)
    {
        std::cout << "Monitor backend is not attached.\n";
        return;
    }

    // Interactive mode:
    // a $C000
    if (args.size() == 2)
    {
        interactiveActive = true;
        interactiveAddress = address;

        std::cout << "Assembly started at $"
                  << std::uppercase << std::hex
                  << std::setw(4) << std::setfill('0')
                  << static_cast<int>(interactiveAddress)
                  << std::dec << std::setfill(' ')
                  << ". Enter blank line or '.' to finish.\n";

        return;
    }

    // One-line mode:
    // a $C000 LDA #$01
    const std::string line = joinArgs(args, 2);

    try
    {
        uint16_t next = address;
        assembleAndWrite(mon, address, line, next);
    }
    catch (const std::exception& e)
    {
        std::cout << "Assembly error: " << e.what() << "\n";
    }
}
