// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Computer.h"
#include "Debug/JamCommand.h"
#include "Debug/MLMonitor.h"
#include "Debug/MLMonitorBackend.h"

JamCommand::JamCommand() = default;

JamCommand::~JamCommand() = default;

int JamCommand::order() const
{
    return 15;
}

std::string JamCommand::name() const
{
    return "jam";
}

std::string JamCommand::category() const
{
    return "Debugging";
}

std::string JamCommand::shortHelp() const
{
    return "jam       - Show or set how JAM/KIL opcodes are handled";
}

std::string JamCommand::help() const
{
    return R"(jam [mode]

Usage:
    jam           Show the current JAM handling mode
    jam freeze    Freeze PC when a JAM/KIL is encountered
    jam halt      Halt CPU execution on JAM/KIL
    jam nop       Treat JAM/KIL as a 2-byte NOP

Description:
    Controls how the emulator reacts when encountering
    undocumented KIL/JAM opcodes. By default, most demos
    expect Freeze or NOP handling instead of a hard halt.
)";
}

void JamCommand::execute(MLMonitor& mon, const std::vector<std::string>& args)
{
    if (args.size() == 1)
    {
        // Show current mode
        std::cout << "The current Jam mode is: " << mon.mlmonitorbackend()->getJamMode() << "\n";
        return;
    }

    if (args.size() == 2 && isHelp(args[1]))
    {
        std::cout << help();
        return;
    }

    // Check input
    if ((args[1] != "freeze" && args[1] != "halt" && args[1] != "nop"))
    {
        std::cout << "Invalid argument! \n";
        std::cout << help();
        return;
    }

    if (args[1] == "freeze")
    {
        mon.mlmonitorbackend()->setJamMode("freeze");
        std::cout << "Updated Jam mode to FreezePC\n";
    }
    else if (args[1] == "halt")
    {
        mon.mlmonitorbackend()->setJamMode("halt");
        std::cout << "Updated Jam mode to Halt.\n";
    }
    else
    {
        mon.mlmonitorbackend()->setJamMode("nop");
        std::cout << "Updated Jam mode to NopCompat\n";
    }
}
