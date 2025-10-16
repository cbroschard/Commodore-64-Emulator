// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Computer.h"
#include "Debug/MLMonitor.h"
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
    return "tape [count]\n"
           "  Display current tape debug info. Shows the pulse index and the next [count] pulses.\n"
           "  If count is omitted, defaults to 8.\n";
}

void TapeCommand::execute(MLMonitor& mon, const std::vector<std::string>& args)
{
    size_t count = 8; // default

    if (args.size() > 1)
    {
        try
        {
            count = std::stoul(args[1]);
        }
        catch (...)
        {
            std::cout << "Invalid argument." << std::endl;
            return;
        }
    }

    std::cout << mon.mlmonitorbackend()->dumpTapeDebug(count);
}
