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

    uint16_t address = parseAddress(args[1]);

    // Interactive mode:
    // a $C000
    if (args.size() == 2)
    {
        interactiveActive = true;
        interactiveAddress = address;
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
