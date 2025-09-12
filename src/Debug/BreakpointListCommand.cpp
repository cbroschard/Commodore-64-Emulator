// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Computer.h"
#include "Debug/BreakpointListCommand.h"
#include "Debug/MLMonitor.h"

BreakpointListCommand::BreakpointListCommand() = default;

BreakpointListCommand::~BreakpointListCommand() = default;

std::string BreakpointListCommand::name() const
{
    return "blist";
}

std::string BreakpointListCommand::category() const
{
    return "Debugging";
}

std::string BreakpointListCommand::shortHelp() const
{
    return "blist     - List all breakpoints";
}

std::string BreakpointListCommand::help() const
{
    return
        "blist\n"
        "    List all currently set breakpoints.\n"
        "    Each breakpoint is shown with its index and address.\n"
        "\n"
        "Notes:\n"
        "    - If no breakpoints are active, a message will be shown.\n"
        "    - Index values can be used as a reference when clearing breakpoints.\n"
        "\n"
        "Examples:\n"
        "    blist       Show all breakpoints\n";
}

void BreakpointListCommand::execute(MLMonitor& mon, const std::vector<std::string>& args)
{
    if (args.size() > 1 && isHelp(args[1]))
    {
        std::cout << help() << std::endl;
        return;
    }

    if (mon.breakpointsEmpty())
    {
        std::cout << "No active breakpoints." << std::endl;
        return;
    }

    std::cout << "Active breakpoints:" << std::endl;
    mon.listBreakpoints();
}
