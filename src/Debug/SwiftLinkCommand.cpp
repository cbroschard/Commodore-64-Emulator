// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Debug/MLMonitor.h"
#include "Debug/MLMonitorBackend.h"
#include "Debug/SwiftLinkCommand.h"

SwiftLinkCommand::SwiftLinkCommand() = default;

SwiftLinkCommand::~SwiftLinkCommand() = default;

int SwiftLinkCommand::order() const
{
    return 20;
}

std::string SwiftLinkCommand::name() const
{
    return "swiftlink";
}

std::string SwiftLinkCommand::category() const
{
    return "Peripheral/User Port";
}

std::string SwiftLinkCommand::shortHelp() const
{
    return "swiftlink                   - Inspect SwiftLink, MOS6551 ACIA, and RS232 state";
}

std::string SwiftLinkCommand::help() const
{
    return
        "swiftlink [all|acia|rs232]\n"
        "  all    - Show all SwiftLink state\n"
        "  acia   - Show MOS6551 register/state information\n"
        "  rs232  - Show SwiftLink RS232 state\n";
}

void SwiftLinkCommand::execute(MLMonitor& mon, const std::vector<std::string>& args)
{
    if (args.size() == 1)
    {
        std::cout << mon.mlmonitorbackend()->swiftLinkDebug("all") << '\n';
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
        std::cout << mon.mlmonitorbackend()->swiftLinkDebug(subCommand) << '\n';
        return;
    }

    std::cout << "Unknown SwiftLink subcommand: " << subCommand << "\n";
    std::cout << "Try: swiftlink help\n";
}
