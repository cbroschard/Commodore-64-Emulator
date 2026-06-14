// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Debug/DisassembleCommand.h"
#include "Debug/MLMonitor.h"
#include "Debug/MLMonitorBackend.h"

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
    return "d [addr] [count] - Disassemble instructions from PC, last address, or address";
}

std::string DisassembleCommand::help() const
{
    return
        "d - Disassemble memory into 6502 instructions\n"
        "\n"
        "Usage:\n"
        "    d\n"
        "    d <address>\n"
        "    d <address> [count]\n"
        "\n"
        "Modes:\n"
        "    d\n"
        "        Disassembles from the current CPU PC the first time.\n"
        "        After that, continues from the previous disassembly position.\n"
        "\n"
        "    d <address>\n"
        "        Disassembles from the specified address.\n"
        "\n"
        "    d <address> [count]\n"
        "        Disassembles the requested number of instructions from address.\n"
        "\n"
        "Arguments:\n"
        "    <address>   Starting address, such as $C000 or C000.\n"
        "    [count]     Optional number of instructions to disassemble.\n"
        "                Defaults to 20.\n"
        "\n"
        "Examples:\n"
        "    d               Disassemble 20 instructions from PC or last position\n"
        "    d $C000         Disassemble 20 instructions from $C000\n"
        "    d C000 50       Disassemble 50 instructions from $C000\n"
        "\n"
        "Notes:\n"
        "    - Output shows address, opcode bytes, and assembly mnemonic.\n"
        "    - The next plain 'd' continues after the previous disassembly.\n";
}

void DisassembleCommand::execute(MLMonitor& mon, const std::vector<std::string>& args)
{
    if (args.size() > 1 && isHelp(args[1]))
    {
        std::cout << help() << std::endl;
        return;
    }

    MLMonitorBackend* backend = mon.mlmonitorbackend();

    if (backend == nullptr)
    {
        std::cout << "Monitor backend is not attached.\n";
        return;
    }

    Memory* mem = backend->getMem();

    if (mem == nullptr)
    {
        std::cout << "Memory is not attached.\n";
        return;
    }

    uint16_t start = 0;
    int count = 20;

    try
    {
        if (args.size() > 1)
        {
            start = parseAddress(args[1]);
            hasLastPC = true;
        }
        else if (hasLastPC)
        {
            start = lastPC;
        }
        else
        {
            start = backend->getPC();
            hasLastPC = true;
        }

        if (args.size() > 2)
        {
            count = std::stoi(args[2], nullptr, 0);

            if (count <= 0)
            {
                std::cout << "Invalid count. Count must be greater than 0.\n";
                return;
            }
        }

        const uint32_t tmpEnd = static_cast<uint32_t>(start) +
                                static_cast<uint32_t>(count * 3);

        const uint16_t end = static_cast<uint16_t>(std::min(tmpEnd, 0xFFFFu));

        lastPC = end;

        const std::string disAsm =
            Disassembler::disassembleRange(start, end, lastPC, *mem);

        std::cout << disAsm << std::endl;
    }
    catch (const std::exception&)
    {
        std::cout << "Error: invalid address or count.\n";
        std::cout << "Usage: d [address] [count]\n";
    }
}
