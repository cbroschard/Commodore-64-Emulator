// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Debug/MLMonitor.h"
#include "Debug/MLMonitorBackend.h"
#include "Debug/TapeCommand.h"

TapeCommand::TapeCommand() = default;

TapeCommand::~TapeCommand() = default;

int TapeCommand::order() const
{
    return 5;
}

std::string TapeCommand::name() const
{
    return "tape";
}

std::string TapeCommand::category() const
{
    return "Hardware/Datasette";
}

std::string TapeCommand::shortHelp() const
{
    return "tape [count] - Show current tape position and upcoming pulses";
}

std::string TapeCommand::help() const
{
    return
        "tape - Show Datasette/tape debug information\n"
        "\n"
        "Usage:\n"
        "    tape\n"
        "    tape [count]\n"
        "\n"
        "Arguments:\n"
        "    [count]    Number of upcoming pulses to show. Defaults to 8.\n"
        "\n"
        "Examples:\n"
        "    tape       Show tape state and next 8 pulses\n"
        "    tape 16    Show tape state and next 16 pulses\n";
}

void TapeCommand::execute(MLMonitor& mon, const std::vector<std::string>& args)
{
    if (args.size() > 1 && isHelp(args[1]))
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

    size_t count = 8;

    if (args.size() > 1)
    {
        try
        {
            count = std::stoul(args[1]);

            if (count == 0)
            {
                std::cout << "Invalid count. Count must be greater than 0.\n";
                return;
            }

            if (count > 256)
            {
                std::cout << "Invalid count. Maximum supported count is 256.\n";
                return;
            }
        }
        catch (...)
        {
            std::cout << "Invalid count: " << args[1] << "\n";
            std::cout << "Usage: tape [count]\n";
            return;
        }
    }

    std::cout << backend->dumpTapeDebug(count);
}
