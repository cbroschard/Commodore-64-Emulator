// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Computer.h"
#include "Debug/MLMonitor.h"
#include "Debug/WatchCommand.h"

WatchCommand::WatchCommand() = default;

WatchCommand::~WatchCommand() = default;

int WatchCommand::order() const
{
    return 20;
}

std::string WatchCommand::name() const
{
    return "watch";
}

std::string WatchCommand::category() const
{
    return "Debugging";
}

std::string WatchCommand::shortHelp() const
{
    return "watch [add|list|clear] ...  — Manage watchpoints";
}

std::string WatchCommand::help() const
{
   return R"(watch - Manage watchpoints

Usage:
  watch <addr>              Add a watchpoint at <addr>
  watch add <addr>          Same as above
  watch list                List all watchpoints
  watch clear               Clear all watchpoints
  watch clear <addr>        Clear watchpoint at <addr>

Examples:
  watch $D020               Break when border color changes
  watch list
  watch clear $D011
  watch clear
)";
}

void WatchCommand::execute(MLMonitor& mon, const std::vector<std::string>& args)
{
    auto parse_addr = [&](const std::string& s) -> uint16_t
    {
        return parseAddress(s);
    };

    // help or no-arg -> show usage
    if (args.size() == 1 || (args.size() > 1 && isHelp(args[1])))
    {
        std::cout << help() << std::endl;
        return;
    }

    const std::string cmd = args[1];

    if (cmd == "list")
    {
        mon.listWatches();
        return;
    }

    if (cmd == "clear")
    {
        if (args.size() == 2)
        { // clear all
            mon.clearAllWatches();
            return;
        }
        try
        { // clear one
            mon.clearWatch(parse_addr(args[2]));
        }
        catch (...) {
            std::cout << "Error: Invalid address.\n" << help() << std::endl;
        }
        return;
    }

    if (cmd == "add")
    {
        if (args.size() < 3)
        {
            std::cout << "Error: Missing address.\n" << help() << std::endl;
            return;
        }
        try
        {
            mon.addWatch(parse_addr(args[2]));
        }
        catch (...)
        {
            std::cout << "Error: Invalid address.\n" << help() << std::endl;
        }
        return;
    }

    // Fallback: treat args[1] as an address (supports "watch $D020")
    try
    {
        mon.addWatch(parse_addr(args[1]));
    }
    catch (...)
    {
        std::cout << "Error: Invalid address.\n" << help() << std::endl;
    }
}
