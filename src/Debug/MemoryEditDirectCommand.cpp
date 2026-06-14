// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Debug/MemoryEditDirectCommand.h"
#include "Debug/MLMonitor.h"
#include "Debug/MLMonitorBackend.h"

MemoryEditDirectCommand::MemoryEditDirectCommand() = default;

MemoryEditDirectCommand::~MemoryEditDirectCommand() = default;

int MemoryEditDirectCommand::order() const
{
    return 15;
}

std::string MemoryEditDirectCommand::name() const
{
    return "ed";
}

std::string MemoryEditDirectCommand::category() const
{
    return "Memory";
}

std::string MemoryEditDirectCommand::shortHelp() const
{
    return "ed <addr> <byte> - Direct RAM edit bypassing bus/PLA";
}

std::string MemoryEditDirectCommand::help() const
{
    return
        "ed - Direct RAM edit\n"
        "\n"
        "Usage:\n"
        "    ed <addr> <byte>\n"
        "\n"
        "Description:\n"
        "    Writes directly to underlying RAM at <addr>, ignoring current memory\n"
        "    mapping such as PLA/$01, ROMs, cartridges, and I/O devices.\n"
        "\n"
        "Arguments:\n"
        "    <addr>     RAM address to write, such as $A000 or D020.\n"
        "    <byte>     Byte value to write, such as $00, $EA, or 255.\n"
        "\n"
        "Notes:\n"
        "    - Use this to modify RAM hidden under BASIC/KERNAL/character ROM or I/O.\n"
        "    - This bypasses device side effects.\n"
        "    - ed $D020 $00 writes RAM under I/O; it does not change the border color.\n"
        "\n"
        "Examples:\n"
        "    ed $A000 $01    Write to RAM under BASIC ROM\n"
        "    ed $D020 $00    Write RAM under I/O, not the VIC border register\n";
}

void MemoryEditDirectCommand::execute(MLMonitor& mon, const std::vector<std::string>& args)
{
    if (args.size() < 2 || isHelp(args[1]))
    {
        std::cout << help() << std::endl;
        return;
    }

    if (args.size() < 3)
    {
        std::cout << "Missing byte value.\n";
        std::cout << "Usage: ed <addr> <byte>\n";
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
            std::cout << "Invalid byte value: " << args[2] << "\n";
            std::cout << "Value must be in range $00-$FF.\n";
            return;
        }

        const uint8_t value = static_cast<uint8_t>(parsedValue);

        backend->writeRAMDirect(address, value);

        std::cout << "Direct wrote $"
                  << std::uppercase << std::hex
                  << std::setw(2) << std::setfill('0')
                  << static_cast<int>(value)
                  << " to RAM $"
                  << std::setw(4) << std::setfill('0')
                  << static_cast<int>(address)
                  << std::dec << std::setfill(' ')
                  << "\n";
    }
    catch (const std::exception&)
    {
        std::cout << "Error: invalid address or byte value.\n";
        std::cout << "Usage: ed <addr> <byte>\n";
    }
}
