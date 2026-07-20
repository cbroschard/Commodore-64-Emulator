// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "6502/Disassembler.h"
#include "Debug/ExportDisassemblyCommand.h"
#include "Debug/MLMonitor.h"
#include "Debug/MLMonitorBackend.h"

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
        "exportdisasm - Export disassembly to a file\n"
        "\n"
        "Usage:\n"
        "    exportdisasm <address> <count> <file>\n"
        "    exportdisasm <address> <endaddr> <file>\n"
        "\n"
        "Arguments:\n"
        "    <address>    Starting address, such as $C000 or C000.\n"
        "    <count>      Number of instructions to disassemble, usually decimal.\n"
        "    <endaddr>    Ending address, such as $C200, 0xC200, or C200.\n"
        "    <file>       Output filename for the disassembly.\n"
        "\n"
        "Examples:\n"
        "    exportdisasm $C000 50 disasm.txt\n"
        "        Export about 50 instructions starting at $C000.\n"
        "\n"
        "    exportdisasm $C000 $C200 disasm.txt\n"
        "        Export instructions from $C000 through $C200.\n"
        "\n"
        "    exportdisasm C000 C200 disasm.txt\n"
        "        Same as above, using bare hex addresses.\n"
        "\n"
        "Notes:\n"
        "    - If the second value looks like an address, it is treated as an end address.\n"
        "    - If the second value is a small decimal number, it is treated as a count.\n";
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
        std::cout << "Error: missing arguments.\n";
        std::cout << "Usage: exportdisasm <address> <count|endaddr> <file>\n";
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

    try
    {
        const uint16_t start = parseAddress(args[1]);
        uint16_t end = 0;
        uint16_t nextAddress = start;

        const std::string& rangeOrCount = args[2];
        const std::string& filename = args[3];

        const bool explicitAddress =
            rangeOrCount.rfind("$", 0) == 0 ||
            rangeOrCount.rfind("0x", 0) == 0 ||
            rangeOrCount.rfind("0X", 0) == 0;

        const bool bareHexAddress =
            rangeOrCount.size() > 2 &&
            rangeOrCount.find_first_of("abcdefABCDEF") != std::string::npos;

        const bool likelyAddress =
            explicitAddress || bareHexAddress || rangeOrCount.size() == 4;

        if (likelyAddress)
        {
            end = parseAddress(rangeOrCount);

            if (end < start)
            {
                std::cout << "Error: end address is before start address.\n";
                return;
            }
        }
        else
        {
            const int count = std::stoi(rangeOrCount, nullptr, 0);

            if (count <= 0)
            {
                std::cout << "Error: count must be greater than 0.\n";
                return;
            }

            const uint32_t tmpEnd =
                static_cast<uint32_t>(start) +
                static_cast<uint32_t>(count * 3);

            end = static_cast<uint16_t>(std::min(tmpEnd, 0xFFFFu));
        }

        const std::string disAsm =
            Disassembler::disassembleRange(start, end, nextAddress, *mem);

        std::ofstream outFile(filename);

        if (!outFile)
        {
            std::cout << "Error: could not open file " << filename << " for writing.\n";
            return;
        }

        outFile << disAsm;

        std::cout << "Disassembly exported to " << filename << "\n";
    }
    catch (const std::exception& e)
    {
        std::cout << "Error: invalid arguments: " << e.what() << "\n";
        std::cout << "Usage: exportdisasm <address> <count|endaddr> <file>\n";
    }
}
