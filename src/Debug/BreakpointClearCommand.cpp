// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Computer.h"
#include "Debug/BreakpointClearCommand.h"
#include "Debug/MLMonitor.h"

BreakpointClearCommand::BreakpointClearCommand() = default;

BreakpointClearCommand::~BreakpointClearCommand() = default;

std::string BreakpointClearCommand::name() const
{
    return "bc";
}

std::string BreakpointClearCommand::category() const
{
    return "Debugging";
}

std::string BreakpointClearCommand::shortHelp() const
{
    return "bc        - Clear breakpoint at address";
}

std::string BreakpointClearCommand::help() const
{
    return
        "bc <address>\n"
        "    Clear a specific breakpoint or all breakpoints if no address is given.\n"
        "\n"
        "Arguments:\n"
        "    <address>   Hexadecimal address (e.g., $C000 or C000).\n"
        "\n"
        "Notes:\n"
        "    - If multiple breakpoints exist, only the one matching the\n"
        "      given address will be removed.\n"
        "    - Use 'blist' to confirm active breakpoints.\n"
        "\n"
        "Examples:\n"
        "    bc $C000    Remove breakpoint at $C000\n";
}

void BreakpointClearCommand::execute(MLMonitor& mon, const std::vector<std::string>& args)
{
    if (args.size() > 1 && isHelp(args[1]))
    {
        std::cout << help() << std::endl;
        return;
    }

    if (mon.breakpointsEmpty())
    {
        std::cout << "There are no breakpoints set!" << std::endl;
        return;
    }

    if (args.size() > 1)
    {
        try
        {
            // Clear a specific breakpoint
            uint16_t addr = parseAddress(args[1]);
            mon.clearBreakpoint(addr);
            std::cout << "Breakpoint cleared at $"
                      << std::uppercase << std::hex << std::setw(4) << std::setfill('0')
                      << addr << std::endl;
        }
        catch(const std::exception& e)
        {
            std::cout << "Error: Invalid address or count. Usage: " << help() << std::endl;
        }
    }
    else
    {
        // Clear all breakpoints
        mon.clearAllBreakpoints();   // add this to MLMonitor if not already there
        std::cout << "All breakpoints cleared." << std::endl;
    }
}
