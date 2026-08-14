// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Debug/MLMonitor.h"
#include "Debug/MLMonitorBackend.h"
#include "Debug/Turbo232Command.h"

Turbo232Command::Turbo232Command() = default;

Turbo232Command::~Turbo232Command() = default;

int Turbo232Command::order() const
{
    return 25;
}

std::string Turbo232Command::name() const
{
    return "turbo232";
}

std::string Turbo232Command::category() const
{
    return "Peripheral/User Port";
}

std::string Turbo232Command::shortHelp() const
{
    return "turbo232                   - Inspect Turbo232, MOS6551 ACIA, and RS232 state";
}

std::string Turbo232Command::help() const
{
    return
        "turbo232 [all|acia|rs232]\n"
        "  all    - Show all Turbo232 state\n"
        "  acia   - Show MOS6551 register/state information\n"
        "  rs232  - Show Turbo232 RS232 state\n";
}

void Turbo232Command::execute(MLMonitor& mon, const std::vector<std::string>& args)
{
    if (args.size() == 1)
    {
        std::cout << mon.mlmonitorbackend()->turbo232Debug("all") << '\n';
        return;
    }

    if (isHelp(args[1]))
    {
        std::cout << help();
        return;
    }

    if (args.size() > 2)
    {
        std::cout << help();
        return;
    }

    const std::string& subCommand = args[1];

    if (subCommand == "all" || subCommand == "acia" || subCommand == "rs232")
    {
        std::cout << mon.mlmonitorbackend()->turbo232Debug(subCommand) << '\n';
        return;
    }

    std::cout << "Unknown turbo232 subcommand: " << subCommand << "\n";
    std::cout << "Try: turbo232 help\n";
}
