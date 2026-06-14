// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Debug/DriveCommand.h"
#include "Debug/MLMonitor.h"
#include "Debug/MLMonitorBackend.h"

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
    return "drive     - Drive status and control";
}

std::string DriveCommand::help() const
{
    return R"(drive - Inspect and control IEC disk drives

Usage:
  drive
  drive list
  drive all
  drive <id>
  drive <id> <subcommand>

General:
  drive                             Show all attached drives
  drive list                        Show all attached drives
  drive all                         Show all attached drives
  drive help                        Show this help text

Drive summary:
  drive <id>                        Show summary for drive ID, such as 8, 9, or 10

Subcommands:
  drive <id> cpu                    Show drive CPU state
  drive <id> mem <addr> [count]     Dump drive memory from address
  drive <id> mem <start>-<end>      Dump drive memory range
  drive <id> via1                   Show VIA1 state, usually 1541/1571
  drive <id> via2                   Show VIA2 state, usually 1541/1571
  drive <id> cia                    Show CIA state, usually 1571/1581
  drive <id> fdc                    Show FDC controller state, usually 1581/1571
  drive <id> state                  Show IEC physical and debug state
  drive <id> step                   Step/tick the drive CPU once
  drive <id> help                   Show this help text

Examples:
  drive
  drive list
  drive 8
  drive 8 cpu
  drive 8 mem $0300
  drive 8 mem $0300 64
  drive 8 mem $0300-$03FF
  drive 8 via1
  drive 8 state
  drive 8 step
)";
}

void DriveCommand::execute(MLMonitor& mon, const std::vector<std::string>& args)
{
    MLMonitorBackend* backend = mon.mlmonitorbackend();

    if (backend == nullptr)
    {
        std::cout << "Monitor backend is not attached.\n";
        return;
    }

    // No args or just "drive" => list all drives.
    // args.empty() is defensive; usually args.size() == 1 for "drive".
    if (args.empty() || args.size() == 1)
    {
        backend->dumpDriveList();
        return;
    }

    const std::string& first = args[1];

    // drive help / drive ?
    if (isHelp(first))
    {
        std::cout << help();
        return;
    }

    // drive list / drive all
    if (args.size() == 2 && (first == "all" || first == "list"))
    {
        backend->dumpDriveList();
        return;
    }

    int id = -1;

    try
    {
        id = std::stoi(first);
    }
    catch (...)
    {
        std::cout << "Error: drive ID must be numeric.\n";
        std::cout << "Try: drive help\n";
        return;
    }

    // drive 8
    if (args.size() == 2)
    {
        backend->dumpDriveSummary(id);
        return;
    }

    const std::string& subcmd = args[2];

    // drive 8 help / drive 8 ?
    if (isHelp(subcmd))
    {
        std::cout << help();
        return;
    }

    if (subcmd == "cpu")
    {
        backend->dumpDriveCPU(id);
        return;
    }

    if (subcmd == "mem")
    {
        if (args.size() < 4)
        {
            std::cout << "Usage:\n";
            std::cout << "  drive " << id << " mem <addr> [count]\n";
            std::cout << "  drive " << id << " mem <start>-<end>\n";
            return;
        }

        uint16_t start = 0;
        uint16_t count = 0; // 0 means backend default.

        try
        {
            const std::string& spec = args[3];

            const bool looksLikeRange =
                (spec.find('-')  != std::string::npos) ||
                (spec.find("..") != std::string::npos) ||
                (spec.find(':')  != std::string::npos);

            if (looksLikeRange)
            {
                auto [a, b] = parseRangePair(spec);

                if (b < a)
                    throw std::runtime_error("Range end is before start");

                start = a;

                const uint32_t len =
                    static_cast<uint32_t>(b) - static_cast<uint32_t>(a) + 1u;

                if (len == 0 || len > 0xFFFFu)
                    throw std::runtime_error("Range too large");

                count = static_cast<uint16_t>(len);
            }
            else
            {
                start = parseAddress(spec);

                if (args.size() >= 5)
                {
                    const uint16_t parsedCount = parseAddress(args[4]);

                    if (parsedCount == 0)
                        throw std::runtime_error("Count must be greater than 0");

                    count = parsedCount;
                }
            }
        }
        catch (const std::exception& e)
        {
            std::cout << "Error parsing mem arguments: " << e.what() << "\n";
            std::cout << "Usage:\n";
            std::cout << "  drive " << id << " mem <addr> [count]\n";
            std::cout << "  drive " << id << " mem <start>-<end>\n";
            return;
        }

        backend->dumpDriveMemory(id, start, count);
        return;
    }

    if (subcmd == "cia")
    {
        backend->dumpDriveCIA(id);
        return;
    }

    if (subcmd == "fdc")
    {
        backend->dumpDriveFDC(id);
        return;
    }

    if (subcmd == "state")
    {
        backend->dumpDriveIECState(id);
        return;
    }

    if (subcmd == "step")
    {
        backend->driveCPUStep(id);
        return;
    }

    if (subcmd == "via1")
    {
        backend->dumpDriveVIA1(id);
        return;
    }

    if (subcmd == "via2")
    {
        backend->dumpDriveVIA2(id);
        return;
    }

    std::cout << "Unknown drive subcommand: " << subcmd << "\n";
    std::cout << "Try: drive help\n";
}
