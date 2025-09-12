// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "GoCommand.h"
#include "Computer.h"
#include "Debug/MLMonitor.h"

GoCommand::GoCommand() = default;

GoCommand::~GoCommand() = default;

std::string GoCommand::name() const
{
    return "g";
}

std::string GoCommand::category() const
{
    return "CPU/Execution";
}

std::string GoCommand::shortHelp() const
{
    return "g         - Start execution";
}

std::string GoCommand::help() const
{
    return "g <addr>           - Start execution at $addr";

}

void GoCommand::execute(MLMonitor& mon, const std::vector<std::string>& args)
{
    if (args.size() > 2 || isHelp(args[1]))
    {
        std::cout << "Usage: " << help() << std::endl;
        return;
    }

    if (args.size() == 1)
    {
        mon.setRunningFlag(false);
        return;
    }
    else
    {
        uint16_t address = parseAddress(args[1]);
        mon.computer()->setPC(address);
        mon.setRunningFlag(false);
        return;
    }

}
