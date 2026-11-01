// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include <algorithm>
#include <cctype>
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
    return "Disk Drives";
}

std::string DriveCommand::shortHelp() const
{
    return "drive     - Drive status and control";
}

std::string DriveCommand::help() const
{
    return R"(drive - Inspect and control IEC and IEEE-488 disk drives

Usage:
  drive
  drive list
  drive all
  drive <id> [subcommand]
  drive <bus> <id> [subcommand]

Bus Selection:
  <bus>       Optional: iec, ieee488, ieee-488
  <id>        Commodore device number (e.g. 8, 9, 10)

  Without a bus selector, the monitor automatically locates
  the drive. If the device number exists on both buses,
  you must explicitly specify the bus.

General:
  drive                       List all attached disk drives
  drive list                  List all attached disk drives
  drive all                   List all attached disk drives
  drive help                  Display this help information

Drive Information:
  drive <id>                  Show drive summary
  drive <id> cpu              Show drive CPU registers and state
  drive <id> mem <addr>        Dump drive memory (default count)
  drive <id> mem <addr> <n>    Dump n bytes from address
  drive <id> mem <start>-<end> Dump a memory address range
  drive <id> state            Show drive bus/debug state
  drive <id> step             Step/tick drive CPU

Drive Hardware:
  drive <id> via1             Show VIA1 registers and timers
  drive <id> via2             Show VIA2 registers and timers
  drive <id> cia              Show CIA registers (if supported)
  drive <id> fdc              Show FDC registers (if supported)

Breakpoints:
  drive <id> bp <addr>              Add CPU breakpoint
  drive <id> bp <addr> if a <value>  Break when A equals value
  drive <id> bp <addr> if x <value>  Break when X equals value
  drive <id> bp <addr> if y <value>  Break when Y equals value
  drive <id> bp list                List CPU breakpoints
  drive <id> clearbp <addr>         Remove CPU breakpoint
  drive <id> clearbp all            Remove all CPU breakpoints

Watchpoints:
  drive <id> watch read <addr>       Add memory read watchpoint
  drive <id> watch write <addr>      Add memory write watchpoint
  drive <id> watch list              List memory watchpoints
  drive <id> clearwatch read <addr>  Remove read watchpoint
  drive <id> clearwatch write <addr> Remove write watchpoint
  drive <id> clearwatch all          Remove all watchpoints

Examples - Automatic Drive Selection:
  drive
  drive list
  drive 8
  drive 8 cpu
  drive 8 mem $0300
  drive 8 mem $0300 64
  drive 8 mem $0300-$03FF
  drive 8 via1
  drive 8 via2
  drive 8 step
  drive 8 bp $80CE
  drive 8 bp list
  drive 8 clearbp all
  drive 8 watch read $1C01
  drive 8 watch write $1800
  drive 8 watch list
  drive 8 clearwatch all

Examples - IEC Drives:
  drive iec 8
  drive iec 8 cpu
  drive iec 8 via1
  drive iec 8 via2
  drive iec 8 mem $0300 64
  drive iec 8 bp $C000
  drive iec 8 watch read $0400
  drive iec 8 step

Examples - IEEE-488 Drives:
  drive ieee488 8
  drive ieee488 8 cpu
  drive ieee488 8 via1
  drive ieee488 8 via2
  drive ieee488 8 mem $0300 64
  drive ieee488 8 bp $C000
  drive ieee488 8 bp list
  drive ieee488 8 clearbp all
  drive ieee488 8 watch read $0400
  drive ieee488 8 watch write $0400
  drive ieee488 8 watch list
  drive ieee488 8 clearwatch all
  drive ieee488 8 step

Notes:
  - All drive commands support optional bus selection.
  - Unsupported hardware components are reported by the monitor.
  - CPU breakpoints and memory watchpoints belong to the selected drive.
  - Use 'iec' for IEC serial-bus diagnostics.
  - Use 'ieee488bus' for IEEE-488 parallel-bus diagnostics.
  - The 'drive state' command is currently IEC-specific
    until IEEE-488 drive-state support is implemented.

Help:
  drive help
  drive <id> help
  drive <bus> <id> help
)";
}

void DriveCommand::execute(MLMonitor& mon, const std::vector<std::string>& rawArgs)
{
    MLMonitorBackend* backend = mon.mlmonitorbackend();

    if (backend == nullptr)
    {
        std::cout << "Monitor backend is not attached.\n";
        return;
    }

    // Default to automatic bus selection.
    MLMonitorBackend::DriveBusType busType = MLMonitorBackend::DriveBusType::Auto;

    std::vector<std::string> args = rawArgs;

    // Support:
    //   drive iec 8 <command>
    //   drive ieee488 8 <command>
    if (args.size() >= 2)
    {
        std::string selector = args[1];

        std::transform(selector.begin(), selector.end(), selector.begin(), [](unsigned char c)
        {
            return static_cast<char>(std::tolower(c));
        });

        if (selector == "iec")
        {
            busType = MLMonitorBackend::DriveBusType::IEC;
            args.erase(args.begin() + 1);
        }
        else if (selector == "ieee488" || selector == "ieee-488")
        {
            busType = MLMonitorBackend::DriveBusType::IEEE488;
            args.erase(args.begin() + 1);
        }

        // An explicit bus selector must include a drive ID.
        if (busType != MLMonitorBackend::DriveBusType::Auto && args.size() < 2)
        {
            std::cout << "Error: missing drive number.\n" << "Usage: drive <iec|ieee488> <id> [subcommand]\n";
            return;
        }
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
        backend->dumpDriveSummary(id, busType);
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
            backend->dumpDriveBreakpoints(id, busType);
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
            backend->clearDriveBreakpoints(id, busType);
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
            backend->dumpDriveWatchpoints(id, busType);
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
            backend->clearDriveWatchpoints(id, busType);
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
        backend->dumpDriveCPU(id, busType);
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
        backend->driveCPUStep(id, busType);
        return;
    }

    if (subcmd == "via1")
    {
        backend->dumpDriveVIA1(id, busType);
        return;
    }

    if (subcmd == "via2")
    {
        backend->dumpDriveVIA2(id, busType);
        return;
    }

    std::cout << "Unknown drive subcommand: " << subcmd << "\n";
    std::cout << "Try: drive help\n";
}
