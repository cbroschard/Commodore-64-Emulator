// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Computer.h"
#include "Debug/MemoryDumpCommand.h"
#include "Debug/MLMonitor.h"

MemoryDumpCommand::MemoryDumpCommand() = default;

MemoryDumpCommand::~MemoryDumpCommand() = default;

std::string MemoryDumpCommand::name() const
{
    return "m";
}

std::string MemoryDumpCommand::category() const
{
    return "Memory";
}

std::string MemoryDumpCommand::shortHelp() const
{
    return "m         - Hex dump memory";
}

std::string MemoryDumpCommand::help() const
{
    return "m <addr> [count]   - hex dump memory at $addr for [count] bytes";

}

void MemoryDumpCommand::execute(MLMonitor& mon, const std::vector<std::string>& args)
{
    if (args.size() < 2 || isHelp(args[1]))
    {
        std::cout << "Usage: " << help() << std::endl;
        return;
    }
    try
    {
        uint16_t address = parseAddress(args[1]);
        int count = args.size() >= 3 ? std::stoi(args[2]) : 16;

        for (int i = 0; i < count; i += 16)
        {
            // print address
            std::cout << std::hex << std::setw(4) << std::setfill('0') << (address + i) << ": ";

            // hex part
            std::string ascii;
            for (int j = 0; j < 16 && (i + j) < count; ++j)
            {
                uint8_t v = mon.computer()->readRAM(address + i + j);
                std::cout << std::hex << std::setw(2) << std::setfill('0') << int(v) << ' ';

                // build ASCII string: printable chars or '.'
                if (v >= 32 && v <= 126)
                    ascii.push_back(static_cast<char>(v));
                else
                    ascii.push_back('.');
            }

            // pad hex column if not full 16
            if ((count - i) < 16)
            {
                int remaining = 16 - (count - i);
                for (int k = 0; k < remaining; ++k)
                    std::cout << "   ";
            }

            // ascii part
            std::cout << " " << ascii << "\n";
        }
    }
    catch (const std::exception& e)
    {
        std::cout << "Error: Invalid address or count. Usage: " << help() << std::endl;
    }
}
