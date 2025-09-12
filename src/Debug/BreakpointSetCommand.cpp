// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Computer.h"
#include "Debug/BreakpointSetCommand.h"
#include "Debug/MLMonitor.h"

BreakpointSetCommand::BreakpointSetCommand() = default;

BreakpointSetCommand::~BreakpointSetCommand() = default;

std::string BreakpointSetCommand::name() const
{
    return "bp";
}

std::string BreakpointSetCommand::category() const
{
    return "Debugging";
}

std::string BreakpointSetCommand::shortHelp() const
{
    return "bp        - Set breakpoint at address";
}

std::string BreakpointSetCommand::help() const
{
    return
        "bp <address>\n"
        "    Set a breakpoint at the given memory address.\n"
        "    When the CPU program counter (PC) reaches this address,\n"
        "    execution will pause and return to the monitor.\n"
        "\n"
        "Arguments:\n"
        "    <address>   Hexadecimal address (e.g., $C000 or C000).\n"
        "\n"
        "Notes:\n"
        "    - Multiple breakpoints can be set; use 'blist' to view them.\n"
        "    - Use 'bc <address>' to clear a specific breakpoint.\n"
        "\n"
        "Examples:\n"
        "    bp $C000     Set a breakpoint at $C000\n"
        "    bp C010      Set a breakpoint at $C010\n";
}

void BreakpointSetCommand::execute(MLMonitor& mon, const std::vector<std::string>& args)
{
    if (args.size() < 2 || isHelp(args[1]))
    {
        std::cout << help() << std::endl;
        return;
    }

    try
    {
        uint16_t address = parseAddress(args[1]); // helper: accepts $C000 or 49152
        mon.addBreakpoint(address);

        std::cout << "Breakpoint set at $"
                  << std::uppercase << std::hex << std::setw(4) << std::setfill('0')
                  << address << std::endl;
    }
    catch(const std::exception& e)
    {
        std::cout << "Error: Invalid address or count. Usage: " << help() << std::endl;
    }
}
