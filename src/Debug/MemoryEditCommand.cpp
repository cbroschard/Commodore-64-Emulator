// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Debug/MemoryEditCommand.h"
#include "Debug/MLMonitor.h"
#include "Debug/MLMonitorBackend.h"

MemoryEditCommand::MemoryEditCommand() = default;

MemoryEditCommand::~MemoryEditCommand() = default;

std::string MemoryEditCommand::name() const
{
    return "e";
}

std::string MemoryEditCommand::category() const
{
    return "Memory";
}

std::string MemoryEditCommand::shortHelp() const
{
    return "e <addr> <value> - Edit memory";
}

std::string MemoryEditCommand::help() const
{
    return
        "e - Edit memory\n"
        "\n"
        "Usage:\n"
        "    e <addr> <value>\n"
        "\n"
        "Arguments:\n"
        "    <addr>     Memory address to write, such as $C000 or C000.\n"
        "    <value>    Byte value to write, such as $00, $FF, or 255.\n"
        "\n"
        "Examples:\n"
        "    e $C000 $EA\n"
        "    e $0801 0\n"
        "    e D020 6\n";
}

void MemoryEditCommand::execute(MLMonitor& mon, const std::vector<std::string>& args)
{
    if (args.size() < 2 || isHelp(args[1]))
    {
        std::cout << help() << std::endl;
        return;
    }

    if (args.size() < 3)
    {
        std::cout << "Missing value.\n";
        std::cout << "Usage: e <addr> <value>\n";
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
        const uint16_t parsedValue = parseAddress(args[2]);

        if (parsedValue > 0xFF)
        {
            std::cout << "Invalid value: " << args[2] << "\n";
            std::cout << "Value must be in byte range $00-$FF.\n";
            return;
        }

        const uint8_t value = static_cast<uint8_t>(parsedValue);

        backend->writeRAM(address, value);

        std::cout << "Wrote $"
                  << std::uppercase << std::hex
                  << std::setw(2) << std::setfill('0')
                  << static_cast<int>(value)
                  << " to $"
                  << std::setw(4) << std::setfill('0')
                  << static_cast<int>(address)
                  << std::dec << std::setfill(' ')
                  << "\n";
    }
    catch (const std::exception&)
    {
        std::cout << "Error: invalid address or value.\n";
        std::cout << "Usage: e <addr> <value>\n";
    }
}
