// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Computer.h"
#include "Debug/DriveCommand.h"
#include "Debug/MLMonitor.h"

DriveCommand::DriveCommand() = default;

DriveCommand::~DriveCommand() = default;

int DriveCommand::order() const
{
    return 15;
}

std::string DriveCommand::name() const
{
    return "drive";
}

std::string DriveCommand::category() const
{
    return "Drives and IEC Bus";
}

std::string DriveCommand::shortHelp() const
{
    return "drive         - Drive status and control";
}

std::string DriveCommand::help() const
{
     return R"(
drive - Inspect IEC disk drives

Usage:
  drive                             Show all attached drives
  drive <id>                        Show summary for drive (8,9,10…)
  drive <id> cpu                    Show drive CPU state
  drive <id> mem address count      Dump memory range
  drive <id> via1                   Show VIA1 state (1541/1571)
  drive <id> via2                   Show VIA2 state
  drive <id> cia                    Show CIA state (1571/1581)
  drive <id> fdc                    Show FDC controller state
  drive <id> state                  Show IEC protocol state
  drive <id> step                   Tick drive once
  drive <id> run <n>                Run drive for n cycles
)";
}

void DriveCommand::execute(MLMonitor& mon, const std::vector<std::string>& args)
{
    // No args or just "drive" => list all drives
    if (args.empty())
    {
        mon.mlmonitorbackend()->dumpDriveList();
        return;
    }

    // First token after "drive"
    std::string first = args.size() >= 2 ? args[1] : std::string();

    // Help: "drive help" or "drive ?"
    if (!first.empty() && isHelp(first))
    {
        std::cout << help();
        return;
    }

    // "drive" or "drive list" / "drive all"
    if (args.size() == 1 ||
        (args.size() == 2 && (first == "all" || first == "list")))
    {
        mon.mlmonitorbackend()->dumpDriveList();
        return;
    }

    // At this point we expect a drive ID in args[1]
    int id = -1;
    try
    {
        id = std::stoi(first);  // args[1] should be the numeric ID (e.g. "8")
    }
    catch (...)
    {
        std::cout << "Error: drive ID must be numeric.\n";
        return;
    }

    // "drive 8" => summary
    if (args.size() == 2)
    {
        mon.mlmonitorbackend()->dumpDriveSummary(id);
        return;
    }

    // Subcommand after ID: e.g. "drive 8 cpu"
    std::string subcmd = args[2];

    if (subcmd == "cpu")
    {
        mon.mlmonitorbackend()->dumpDriveCPU(id);
        return;
    }
    else if (subcmd == "mem")
    {
        std::cout << "To be implemented.\n";
        return;
    }
    else if (subcmd == "cia")
    {
        mon.mlmonitorbackend()->dumpDriveCIA(id);
        return;
    }
    else if (subcmd == "fdc")
    {
        mon.mlmonitorbackend()->dumpDriveFDC(id);
        return;
    }
    else if (subcmd == "via1")
    {
        mon.mlmonitorbackend()->dumpDriveVIA1(id);
        return;
    }
    else if (subcmd == "via2")
    {
        mon.mlmonitorbackend()->dumpDriveVIA2(id);
        return;
    }

    // If we get here, it's an unknown subcommand:
    std::cout << "Unknown drive subcommand: " << subcmd << "\n";
    std::cout << help();
}
