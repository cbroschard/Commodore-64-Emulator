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
    return "tape [pulses|list|select|load] - Inspect and control tape media";
}

std::string TapeCommand::help() const
{
    return
        "tape - Inspect and control Datasette/tape media\n"
        "\n"
        "Usage:\n"
        "    tape\n"
        "    tape [count]\n"
        "    tape pulses [count]\n"
        "    tape list\n"
        "    tape select <index>\n"
        "    tape load\n"
        "\n"
        "Commands:\n"
        "    pulses [count]   Show current pulse state and upcoming pulses.\n"
        "                     Defaults to 8 pulses.\n"
        "    list             List files contained in the loaded T64 image.\n"
        "    select <index>   Select a T64 directory entry.\n"
        "    load             Load the currently selected T64 entry into memory.\n"
        "\n"
        "Examples:\n"
        "    tape             Show general tape state\n"
        "    tape 16          Show the next 16 pulses\n"
        "    tape pulses 16   Show the next 16 pulses\n"
        "    tape list        List T64 entries\n"
        "    tape select 2    Select T64 entry 2\n"
        "    tape load        Load the selected T64 entry\n";
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

    if (args.size() == 1)
    {
        std::cout << backend->dumpTapeDebug(8);
        return;
    }

    const std::string& command = args[1];

    if (command == "pulses")
    {
        size_t count = 8;

        if (args.size() > 2)
        {
            try
            {
                count = std::stoul(args[2]);

                if (count == 0 || count > 256)
                {
                    std::cout << "Invalid count. Valid range is 1-256.\n";
                    return;
                }
            }
            catch (...)
            {
                std::cout << "Invalid count: " << args[2] << "\n";
                return;
            }
        }

        std::cout << backend->dumpTapeDebug(count);
        return;
    }

    if (command == "list")
    {
        std::cout << backend->dumpT64Entries();
        return;
    }

    if (command == "select")
    {
        if (args.size() < 3)
        {
            std::cout << "Usage: tape select <index>\n";
            return;
        }

        try
        {
            const size_t index = std::stoul(args[2]);

            if (!backend->selectT64Entry(index))
            {
                std::cout << "Unable to select T64 entry " << index << ".\n";
                return;
            }

            std::cout << "Selected T64 entry " << index << ".\n";
        }
        catch (...)
        {
           std::cout << "Invalid T64 entry index: " << args[2] << "\n";
        }

        return;
    }

    if (command == "load")
    {
        if (!backend->loadSelectedT64Entry())
        {
            std::cout << "Unable to load selected T64 entry.\n";
            return;
        }

        std::cout << "T64 entry loaded.\n";
        return;
    }

    // Backward-compatible shorthand: tape <count>
    try
    {
        const size_t count = std::stoul(command);

        if (count == 0 || count > 256)
        {
            std::cout << "Invalid count. Valid range is 1-256.\n";
            return;
        }

        std::cout << backend->dumpTapeDebug(count);
        return;
    }
    catch (...)
    {

    }

    std::cout << "Unknown tape command: " << command << "\n";
    std::cout << "Use 'tape help' for usage.\n";
}
