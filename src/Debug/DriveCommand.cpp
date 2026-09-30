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

Breakpoints:
  drive <id> bp <addr>                    Add a drive CPU breakpoint
  drive <id> bp <addr> if a <value>       Break when A equals value
  drive <id> bp <addr> if x <value>       Break when X equals value
  drive <id> bp <addr> if y <value>       Break when Y equals value
  drive <id> bp list                      List drive CPU breakpoints
  drive <id> clearbp <addr>               Remove breakpoint(s) at address
  drive <id> clearbp all                  Remove all drive CPU breakpoints

Watchpoints:
  drive <id> watch read <addr>          Break on drive memory read
  drive <id> watch write <addr>         Break on drive memory write
  drive <id> watch list                 List drive watchpoints
  drive <id> clearwatch read <addr>     Remove read watchpoint
  drive <id> clearwatch write <addr>    Remove write watchpoint
  drive <id> clearwatch all             Remove all drive watchpoints

Help:
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
  drive 8 bp $80CE
  drive 8 bp list
  drive 8 clearbp $80CE
  drive 8 clearbp all
  drive 8 watch read $1C01
  drive 8 watch write $1800
  drive 8 watch list
  drive 8 clearwatch all
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

    if (subcmd == "bp")
    {
        if (args.size() < 4)
        {
            std::cout << "Usage:\n";
            std::cout << "  drive " << id << " bp <address>\n";
            std::cout << "  drive " << id << " bp <address> if <a|x|y> <value>\n";
            std::cout << "  drive " << id << " bp list\n";
            return;
        }

        // drive 8 bp list
        if (args[3] == "list")
        {
            backend->dumpDriveBreakpoints(id);
            return;
        }

        try
        {
            const uint16_t address = parseAddress(args[3]);

            // Normal breakpoint:
            // drive 8 bp $82BA
            if (args.size() == 4)
            {
                backend->addDriveBreakpoint(id, address);
                return;
            }

            // Conditional breakpoint:
            // drive 8 bp $82BA if a $00
            if (args.size() == 7 && args[4] == "if")
            {
                const std::string& reg = args[5];
                const uint8_t value = static_cast<uint8_t>(parseAddress(args[6]) & 0xFF);

                Drive::DriveBreakpoint::Condition condition;

                if (reg == "a" || reg == "A")
                {
                    condition = Drive::DriveBreakpoint::Condition::AEquals;
                }
                else if (reg == "x" || reg == "X")
                {
                    condition = Drive::DriveBreakpoint::Condition::XEquals;
                }
                else if (reg == "y" || reg == "Y")
                {
                    condition = Drive::DriveBreakpoint::Condition::YEquals;
                }
                else
                {
                    std::cout << "Invalid breakpoint register: " << reg << "\n";
                    std::cout << "Supported registers: a, x, y\n";
                    return;
                }

                backend->addDriveConditionalBreakpoint(id, address, condition, value);

                return;
            }

            std::cout << "Usage:\n";
            std::cout << "  drive " << id << " bp <address>\n";
            std::cout << "  drive " << id
                      << " bp <address> if <a|x|y> <value>\n";
            std::cout << "  drive " << id << " bp list\n";
        }
        catch (const std::exception& e)
        {
            std::cout << "Invalid breakpoint: " << e.what() << "\n";
        }

        return;
    }

    if (subcmd == "clearbp")
    {
        if (args.size() < 4)
        {
            std::cout << "Usage: drive " << id
                      << " clearbp <address|all>\n";
            return;
        }

        // drive 8 clearbp all
        if (args[3] == "all")
        {
            backend->clearDriveBreakpoints(id);
            return;
        }

        // drive 8 clearbp <address>
        try
        {
            const uint16_t address = parseAddress(args[3]);
            backend->removeDriveBreakpoint(id, address);
        }
        catch (const std::exception& e)
        {
            std::cout << "Invalid breakpoint address: "
                      << e.what() << "\n";
        }

        return;
    }

    if (subcmd == "watch")
    {
        if (args.size() < 4)
        {
            std::cout << "Usage:\n";
            std::cout << "  drive " << id << " watch read <address>\n";
            std::cout << "  drive " << id << " watch write <address>\n";
            std::cout << "  drive " << id << " watch list\n";
            return;
        }

        if (args[3] == "list")
        {
            backend->dumpDriveWatchpoints(id);
            return;
        }

        if (args.size() != 5)
        {
            std::cout << "Usage:\n";
            std::cout << "  drive " << id << " watch read <address>\n";
            std::cout << "  drive " << id << " watch write <address>\n";
            return;
        }

        const std::string& type = args[3];

        try
        {
            const uint16_t address = parseAddress(args[4]);

            if (type == "read")
            {
                backend->addDriveReadWatchpoint(id, address);
            }
            else if (type == "write")
            {
                backend->addDriveWriteWatchpoint(id, address);
            }
            else
            {
                std::cout << "Invalid watchpoint type: "
                          << type << "\n";
                std::cout << "Supported types: read, write\n";
            }
        }
        catch (const std::exception& e)
        {
            std::cout << "Invalid watchpoint address: "
                      << e.what() << "\n";
        }

        return;
    }

    if (subcmd == "clearwatch")
    {
        if (args.size() < 4)
        {
            std::cout << "Usage:\n";
            std::cout << "  drive " << id
                      << " clearwatch <read|write> <address>\n";
            std::cout << "  drive " << id
                      << " clearwatch all\n";
            return;
        }

        if (args[3] == "all")
        {
            backend->clearDriveWatchpoints(id);
            return;
        }

        if (args.size() != 5)
        {
            std::cout << "Usage:\n";
            std::cout << "  drive " << id
                      << " clearwatch <read|write> <address>\n";
            return;
        }

        const std::string& type = args[3];

        try
        {
            const uint16_t address = parseAddress(args[4]);

            if (type == "read")
            {
                backend->removeDriveReadWatchpoint(id, address);
            }
            else if (type == "write")
            {
                backend->removeDriveWriteWatchpoint(id, address);
            }
            else
            {
                std::cout << "Invalid watchpoint type: "
                          << type << "\n";
                std::cout << "Supported types: read, write\n";
            }
        }
        catch (const std::exception& e)
        {
            std::cout << "Invalid watchpoint address: "
                      << e.what() << "\n";
        }

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
