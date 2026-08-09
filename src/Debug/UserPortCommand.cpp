// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Debug/MLMonitor.h"
#include "Debug/MLMonitorbackend.h"
#include "Debug/UserPortCommand.h"

UserPortCommand::UserPortCommand() = default;

UserPortCommand::~UserPortCommand() = default;

int UserPortCommand::order() const
{
    return 20;
}

std::string UserPortCommand::name() const
{
    return "userport";
}

std::string UserPortCommand::category() const
{
    return "Peripheral/User Port";
}

std::string UserPortCommand::shortHelp() const
{
    return "userport [subcmd] - Inspect User Port state, lines, and attached device";
}

std::string UserPortCommand::help() const
{
    return R"(userport - Inspect the C64 User Port

Usage:
    userport [subcommand]

Subcommands:
    status    - Show User Port and attached device state
    device    - Show the attached User Port device
    lines     - Show C64-side User Port signal mapping and line state
    rs232     - Show RS-232 adapter and serial device state
    help      - Show this help text

Examples:
    userport
    userport status
    userport device
    userport lines
    userport rs232
)";
}

void UserPortCommand::execute(MLMonitor& mon, const std::vector<std::string>& args)
{
    // userport help
    // userport ?
    if (args.size() >= 2 &&
        (args[1] == "help" || args[1] == "?"))
    {
        std::cout << help();
        return;
    }

    MLMonitorBackend* backend = mon.mlmonitorbackend();

    if (backend == nullptr)
    {
        std::cout << "Monitor backend is not attached.\n";
        return;
    }

    // Bare "userport" is the same as "userport status".
    if (args.size() == 1)
    {
        std::cout << backend->dumpUserPort();
        return;
    }

    const std::string& subcmd = args[1];

    if (subcmd == "status")
    {
        std::cout << backend->dumpUserPort();
    }
    else if (subcmd == "device")
    {
        std::cout << backend->dumpUserPort();
    }
    else if (subcmd == "lines")
    {
        std::cout << backend->dumpUserPort();
    }
    else if (subcmd == "rs232")
    {
        std::cout << backend->dumpUserPortRS232();
    }
    else
    {
        std::cout << "Unknown User Port subcommand: "
                  << subcmd << "\n";
        std::cout << "Try: userport help\n";
    }
}
