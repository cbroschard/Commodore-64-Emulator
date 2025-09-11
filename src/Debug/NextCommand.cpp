// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Computer.h"
#include "Debug/MLMonitor.h"
#include "Debug/NextCommand.h"

NextCommand::NextCommand() = default;

NextCommand::~NextCommand() = default;

int NextCommand::order() const
{
    return 5;
}

std::string NextCommand::name() const
{
    return "n";
}

std::string NextCommand::category() const
{
    return "CPU/Execution";
}

std::string NextCommand::shortHelp() const
{
    return
        "n         - Step over subroutine";
}

std::string NextCommand::help() const
{
    return
        "n\n"
        "    Step over the current instruction.\n"
        "    If the instruction at the current PC is a JSR (subroutine call),\n"
        "    execution continues until the matching RTS returns, and then control\n"
        "    returns to the monitor.\n"
        "    Otherwise, behaves the same as 't' (single step).\n"
        "\n"
        "Examples:\n"
        "    n        Step over one instruction\n";
}

void NextCommand::execute(MLMonitor& mon, const std::vector<std::string>& args)
{
    if (args.size() > 1 && isHelp(args[1]))
    {
        std::cout << help() << std::endl;
        return;
    }

    uint16_t currentPC = mon.computer()->getPC();
    uint8_t opCode = mon.computer()->readRAM(currentPC);
    if (opCode == 0x20)
    {
        // Computer the address of the instruction after the JSR
        uint16_t targetPC = (currentPC + 3) & 0xFFFF;

        // Set a temporary breakpoint

        // Run until hitting the breakpoint then stop
    }
    else
    {
        mon.computer()->cpuStep();
    }
}
