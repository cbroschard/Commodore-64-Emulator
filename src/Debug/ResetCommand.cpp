// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Debug/MLMonitor.h"
#include "Debug/MLMonitorBackend.h"
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
    return "reset [warm|cold] - Reset the computer";
}

std::string ResetCommand::help() const
{
    return
        "reset - Reset the emulated computer\n"
        "\n"
        "Usage:\n"
        "    reset\n"
        "    reset warm\n"
        "    reset cold\n"
        "\n"
        "Arguments:\n"
        "    warm   Perform a warm reset. This is the default.\n"
        "    cold   Perform a cold reset / power cycle.\n"
        "\n"
        "Examples:\n"
        "    reset         Perform a warm reset\n"
        "    reset warm    Perform a warm reset\n"
        "    reset cold    Perform a full cold reset\n";
}

void ResetCommand::execute(MLMonitor& mon, const std::vector<std::string>& args)
{
    if (args.size() > 1 && isHelp(args[1]))
    {
        std::cout << help() << std::endl;
        return;
    }

    MLMonitorBackend* backend = mon.mlmonitorbackend();

    if (backend == nullptr)
    {
        std::cout << "Monitor backend is not attached.\n";
        return;
    }

    if (args.size() == 1 || args[1] == "warm")
    {
        backend->warmReset();
        std::cout << "Warm reset performed.\n";
        return;
    }

    if (args[1] == "cold")
    {
        backend->coldReset();
        std::cout << "Cold reset performed.\n";
        return;
    }

    std::cout << "Unknown reset type: " << args[1] << "\n";
    std::cout << "Usage: reset [warm|cold]\n";
}
