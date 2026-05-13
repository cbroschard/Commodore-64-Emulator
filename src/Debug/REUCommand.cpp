// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Debug/MLMonitorBackend.h"
#include "REUCommand.h"

REUCommand::REUCommand()
{

}

REUCommand::~REUCommand() = default;

int REUCommand::order() const
{
    return 5;
}

std::string REUCommand::name() const
{
    return "reu";
}

std::string REUCommand::category() const
{
    return "Hardware/REU";
}

std::string REUCommand::shortHelp() const
{
    return "reu       - REU status, registers, and RAM dump";
}

std::string REUCommand::help() const
{
    return R"(
reu - Inspect RAM Expansion Unit

Usage:
  reu                              Show REU attachment, model, size, and decoded state
  reu regs                         Show compact $DF00-$DF0A register values
  reu dump address [count]         Dump REU RAM at REU address
)";
}

void REUCommand::execute(MLMonitor& mon, const std::vector<std::string>& args)
{
    if (args.size() == 1)
    {
        std::cout << mon.mlmonitorbackend()->reuDumpStatus();
        return;
    }

    else if (args.size() == 2 && isHelp(args[1]))
    {
        std::cout << help();
        return;
    }

    const std::string subcmd = args[1];

    if (subcmd == "regs")
    {
        std::cout << mon.mlmonitorbackend()->reuDumpRegs();
        return;
    }

    else if (subcmd == "dump")
    {
        try
        {
            uint32_t address = std::stoi(args[2]);
            std::cout << mon.mlmonitorbackend()->reuDumpRAM(address);
            return;
        }
        catch(const std::exception& e)
        {
            std::cout << "Invalid address!\n";
            return;
        }
    }
}
