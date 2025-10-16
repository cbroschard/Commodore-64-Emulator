// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Computer.h"
#include "Debug/MLMonitor.h"
#include "Debug/ResetCommand.h"

ResetCommand::ResetCommand() = default;

ResetCommand::~ResetCommand() = default;

std::string ResetCommand::name() const
{
    return "reset";
}

std::string ResetCommand::category() const
{
    return "System";
}

std::string ResetCommand::shortHelp() const
{
    return "reset     - Reset the computer (warm or cold)";
}

std::string ResetCommand::help() const
{
    return
            "reset [warm|cold]\n"
            "    Reset the emulated computer.\n"
            "\n"
            "Arguments:\n"
            "    warm   Perform a warm reset (default).\n"
            "    cold   Perform a cold reset (power cycle).\n"
            "\n"
            "Examples:\n"
            "    reset         Perform a warm reset\n"
            "    reset cold    Perform a full cold reset\n";
}

void ResetCommand::execute(MLMonitor& mon, const std::vector<std::string>& args)
{
     if (args.size() > 1 && isHelp(args[1]))
    {
        std::cout << help() << std::endl;
        return;
    }

    if (args.size() > 1 && args[1] == "cold")
    {
        mon.mlmonitorbackend()->coldReset();
    }
    else
    {
        mon.mlmonitorbackend()->warmReset();
    }
}
