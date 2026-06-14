// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Debug/MemoryDumpCommand.h"
#include "Debug/MLMonitor.h"
#include "Debug/MLMonitorBackend.h"

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
    return "m <addr> [count] - Hex dump memory";
}

std::string MemoryDumpCommand::help() const
{
    return
        "m - Hex dump memory\n"
        "\n"
        "Usage:\n"
        "    m <addr>\n"
        "    m <addr> [count]\n"
        "\n"
        "Arguments:\n"
        "    <addr>     Starting memory address, such as $C000 or C000.\n"
        "    [count]    Number of bytes to dump. Defaults to 16.\n"
        "\n"
        "Examples:\n"
        "    m $0800\n"
        "    m $C000 64\n"
        "    m D000 128\n";
}

void MemoryDumpCommand::execute(MLMonitor& mon, const std::vector<std::string>& args)
{
    if (args.size() < 2 || isHelp(args[1]))
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

    try
    {
        const uint16_t address = parseAddress(args[1]);
        int count = 16;

        if (args.size() >= 3)
        {
            count = std::stoi(args[2], nullptr, 0);

            if (count <= 0)
            {
                std::cout << "Invalid count. Count must be greater than 0.\n";
                return;
            }
        }

        for (int i = 0; i < count; i += 16)
        {
            const uint16_t lineAddress = static_cast<uint16_t>(address + i);

            std::cout << std::uppercase << std::hex
                      << std::setw(4) << std::setfill('0')
                      << static_cast<int>(lineAddress)
                      << ": ";

            std::string ascii;

            for (int j = 0; j < 16 && (i + j) < count; ++j)
            {
                const uint16_t readAddress =
                    static_cast<uint16_t>(address + i + j);

                const uint8_t v = backend->readRAM(readAddress);

                std::cout << std::setw(2) << std::setfill('0')
                          << static_cast<int>(v)
                          << ' ';

                if (v >= 32 && v <= 126)
                    ascii.push_back(static_cast<char>(v));
                else
                    ascii.push_back('.');
            }

            if ((count - i) < 16)
            {
                const int remaining = 16 - (count - i);

                for (int k = 0; k < remaining; ++k)
                    std::cout << "   ";
            }

            std::cout << " " << ascii << "\n";
        }

        std::cout << std::dec << std::setfill(' ');
    }
    catch (const std::exception& e)
    {
        std::cout << "Error: invalid address or count.\n";
        std::cout << "Usage: m <addr> [count]\n";
    }
}
