// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include <iostream>
#include "Debug/IEEE488BusCommand.h"
#include "Debug/MLMonitor.h"
#include "Debug/MLMonitorBackend.h"

IEEE488BusCommand::IEEE488BusCommand() = default;

IEEE488BusCommand::~IEEE488BusCommand() = default;

int IEEE488BusCommand::order() const
{
    return 5;
}

std::string IEEE488BusCommand::name() const
{
    return "ieee488bus";
}

std::string IEEE488BusCommand::category() const
{
    return "Devices and IEEE-488 Bus";
}

std::string IEEE488BusCommand::shortHelp() const
{
    return "ieee488bus - Show IEEE-488 bus status";
}

std::string IEEE488BusCommand::help() const
{
    return
        "IEEE-488 Bus Commands\n"
        "---------------------\n"
        "ieee488bus\n"
        "    Display IEEE-488 bus signal levels,\n"
        "    parallel data bus, connected devices,\n"
        "    drive count and pending transitions.\n"
        "\n"
        "ieee488bus status\n"
        "    Display the current physical bus state.\n"
        "\n"
        "ieee488bus help\n"
        "    Display this help information.\n";
}

void IEEE488BusCommand::execute(MLMonitor& mon, const std::vector<std::string>& args)
{
    auto* backend = mon.mlmonitorbackend();

    if (!backend)
    {
        std::cout << "Monitor backend not attached.\n";
        return;
    }

    if (args.size() > 2)
    {
        std::cout << help();
        return;
    }

    if (args.size() == 2)
    {
        if (isHelp(args[1]))
        {
            std::cout << help();
            return;
        }

        if (args[1] != "status")
        {
            std::cout << "Unknown IEEE-488 subcommand: "  << args[1] << "\n";
            std::cout << help();
            return;
        }
    }

    std::cout << backend->dumpIEEE488Bus();
}
