// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Computer.h"
#include "Debug/DisassembleCommand.h"
#include "Debug/MLMonitor.h"

DisassembleCommand::DisassembleCommand() :
    hasLastPC(false)
{

}

DisassembleCommand::~DisassembleCommand() = default;

int DisassembleCommand::order() const
{
    return 4;
}

std::string DisassembleCommand::name() const
{
    return "d";
}

std::string DisassembleCommand::category() const
{
    return "Debugging";
}

std::string DisassembleCommand::shortHelp() const
{
    return "d <addr> [count] - Disassemble instructions starting at address";
}

std::string DisassembleCommand::help() const
{
    return
        "d <address> [count]\n"
        "    Disassemble memory into 6502 assembly instructions starting\n"
        "    at the specified address.\n"
        "\n"
        "Arguments:\n"
        "    <address>   Starting address to begin disassembly (hex, e.g. $C000).\n"
        "    [count]     Optional number of instructions to disassemble.\n"
        "                Defaults to 20 if not provided.\n"
        "\n"
        "Examples:\n"
        "    d $C000         Disassembles 20 instructions starting at $C000.\n"
        "    d C000 50       Disassembles 50 instructions starting at $C000.\n"
        "\n"
        "Notes:\n"
        "    - The output shows address, opcode bytes, and assembly mnemonic.\n"
        "    - You can use labels or symbols in future versions if symbol table\n"
        "      support is added.\n";
}

void DisassembleCommand::execute(MLMonitor& mon, const std::vector<std::string>& args)
{
    if (args.size() > 1 && isHelp(args[1]))
    {
        std::cout << help() << std::endl;
        return;
    }

    try
    {
        Memory* mem = mon.mlmonitorbackend()->getMem();

        uint16_t start = 0;
        uint16_t end = 0;
        int count = 20; // default # of records

        if (args.size() > 1)
        {
            start = parseAddress(args[1]);
            hasLastPC = true;
        }
        else if(hasLastPC)
        {
            start = lastPC;
        }
        else
        {
            start = mon.mlmonitorbackend()->getPC();
            hasLastPC = true;
        }

        if (args.size() > 2) count = std::stoi(args[2], nullptr, 0); // decimal count

        uint32_t tmpEnd = static_cast<uint32_t>(start) + (count * 3);
        end = static_cast<uint16_t>(std::min(tmpEnd, 0xFFFFu));
        //end = start + (count * 3);
        lastPC = end;

        // Execute the disassembly
        std::string disAsm = Disassembler::disassembleRange(start, end, lastPC, *mem);
        std::cout << disAsm << std::endl;
    }
    catch(const std::exception& e)
    {
        std::cout << "Error: Invalid address or value. Usage: " << help() << std::endl;
    }
}
