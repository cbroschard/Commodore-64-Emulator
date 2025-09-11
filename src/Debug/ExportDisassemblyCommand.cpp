// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Computer.h"
#include "Debug/ExportDisassemblyCommand.h"
#include "Debug/MLMonitor.h"

ExportDisassemblyCommand::ExportDisassemblyCommand() = default;

ExportDisassemblyCommand::~ExportDisassemblyCommand() = default;

int ExportDisassemblyCommand::order() const
{
    return 5;
}

std::string ExportDisassemblyCommand::name() const
{
    return "exportdisasm";
}

std::string ExportDisassemblyCommand::category() const
{
    return "Debugging";
}

std::string ExportDisassemblyCommand::shortHelp() const
{
    return "exportdisasm <addr> <count|endaddr> <file> - Export disassembly to file";
}

std::string ExportDisassemblyCommand::help() const
{
    return
        "exportdisasm <address> <count|endaddr> <file>\n"
        "    Disassemble memory into 6502 assembly instructions starting\n"
        "    at the specified address and write the output to a file.\n"
        "\n"
        "Arguments:\n"
        "    <address>    Starting address (hex, e.g. $C000)\n"
        "    <count>      Number of instructions to disassemble (decimal), OR\n"
        "    <endaddr>    Ending address (hex, e.g. $C200)\n"
        "    <file>       Output filename for the disassembly\n"
        "\n"
        "Examples:\n"
        "    exportdisasm $C000 50 disasm.txt\n"
        "        Exports 50 instructions starting at $C000 into disasm.txt\n"
        "    exportdisasm $C000 $C200 disasm.txt\n"
        "        Exports instructions from $C000 up to (and including) $C200\n";
}

void ExportDisassemblyCommand::execute(MLMonitor& mon, const std::vector<std::string>& args)
{
    if (args.size() > 1 && isHelp(args[1]))
    {
        std::cout << help() << std::endl;
        return;
    }

    if (args.size() < 4)
    {
        std::cout << "Error: Missing arguments.\n" << help() << std::endl;
        return;
    }

    try
    {
        Memory* mem = mon.computer()->getMem();

        uint16_t start = parseAddress(args[1]);
        uint16_t end = 0;
        std::string filename = args[3];

        // Try to detect if second arg is hex (end address) or decimal (count)
        if (args[2].rfind("$", 0) == 0 || args[2].find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
        {
            // Treat as address
            end = parseAddress(args[2]);
        }
        else
        {
            // Treat as count
            int count = std::stoi(args[2], nullptr, 0);
            end = start + (count * 3); // worst-case instruction length assumption
        }

        // Perform the disassembly
        std::string disAsm = Disassembler::disassembleRange(start, end, start, *mem);

        // Write to file
        std::ofstream outFile(filename);
        if (!outFile)
        {
            std::cout << "Error: Could not open file " << filename << " for writing." << std::endl;
            return;
        }

        outFile << disAsm;
        outFile.close();

        std::cout << "Disassembly exported to " << filename << std::endl;
    }
    catch(const std::exception& e)
    {
        std::cout << "Error: Invalid arguments. Usage:\n" << help() << std::endl;
    }
}
