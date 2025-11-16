// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
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
  drive                  Show all attached drives
  drive <id>             Show summary for drive (8,9,10…)
  drive <id> cpu         Show drive CPU state
  drive <id> mem a b     Dump memory range
  drive <id> via1        Show VIA1 state (1541/1571)
  drive <id> via2        Show VIA2 state
  drive <id> cia         Show CIA state (1581)
  drive <id> fdc         Show FDC controller state
  drive <id> state       Show IEC protocol state
  drive <id> step        Tick drive once
  drive <id> run <n>     Run drive for n cycles
)";
}

void DriveCommand::execute(MLMonitor& mon, const std::vector<std::string>& args)
{

}
