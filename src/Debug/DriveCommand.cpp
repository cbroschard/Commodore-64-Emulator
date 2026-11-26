// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <stdexcept>
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
  <bus>                        Optional: iec, ieee488, ieee-488
  <id>                         Commodore device number (8, 9, 10, etc.)

  Without a bus selector, the monitor automatically locates
  the drive. If the device number exists on both buses,
  you must explicitly specify the bus.

General:
  drive                        List all attached disk drives
  drive list                   List all attached disk drives
  drive all                    List all attached disk drives
  drive help                   Display this help information

Drive Information:
  drive <id>                   Show drive summary
  drive <id> state             Show drive bus/debug state

CPU Inspection:
  drive <id> cpu               Show default CPU registers
  drive <id> cpu <cpu-id>      Show selected CPU registers

CPU Execution:
  drive <id> step              Execute one instruction on CPU 0
  drive <id> step <cpu-id>     Execute one instruction on selected CPU
  drive <id> cycle             Advance one cycle on CPU 0
  drive <id> cycle <cpu-id>    Advance one cycle on selected CPU

Disk Management:
  drive <id> mount <unit> <path>   Mount disk image in mechanism
  drive <id> unmount <unit>        Unmount disk from mechanism

Memory Inspection:
  drive <id> mem <addr>                  Dump default CPU memory
  drive <id> mem <addr> <count>          Dump memory bytes
  drive <id> mem <start>-<end>           Dump memory range
  drive <id> mem cpu <cpu-id> <addr>     Dump selected CPU memory
  drive <id> mem cpu <cpu-id> <addr> <n> Dump selected CPU memory bytes

Drive Hardware:
  drive <id> hardware          List available hardware components
  drive <id> hw                Short form of hardware
  drive <id> chip <hw-id>      Inspect hardware component registers
  drive <id> via1              Show VIA1 registers and timers
  drive <id> via2              Show VIA2 registers and timers
  drive <id> cia               Show CIA registers (if supported)
  drive <id> fdc               Show FDC registers (if supported)

Breakpoints:
  drive <id> bp <addr>                       Add CPU 0 breakpoint
  drive <id> bp cpu <cpu-id> <addr>          Add selected CPU breakpoint
  drive <id> bp <addr> if a <value>          Break when A equals value
  drive <id> bp <addr> if x <value>          Break when X equals value
  drive <id> bp <addr> if y <value>          Break when Y equals value
  drive <id> bp cpu <id> <addr> if a <value> Conditional CPU breakpoint
  drive <id> bp list                         List all CPU breakpoints
  drive <id> clearbp <addr>                  Remove CPU 0 breakpoint
  drive <id> clearbp cpu <id> <addr>         Remove selected CPU breakpoint
  drive <id> clearbp all                     Remove all CPU breakpoints

Watchpoints:
  drive <id> watch read <addr>        Add memory read watchpoint
  drive <id> watch write <addr>       Add memory write watchpoint
  drive <id> watch list              List memory watchpoints
  drive <id> clearwatch read <addr>   Remove read watchpoint
  drive <id> clearwatch write <addr>  Remove write watchpoint
  drive <id> clearwatch all           Remove all watchpoints

Examples - General:
  drive
  drive list
  drive 8
  drive 8 hardware
  drive 8 cpu
  drive 8 mem $0300
  drive 8 mem $0300 64
  drive 8 mem $0300-$03FF
  drive 8 step
  drive 8 cycle

Examples - Hardware Inspection:
  drive 8 hardware
  drive 8 chip 3
  drive 8 chip 4
  drive 8 via1
  drive 8 via2
  drive 8 cia
  drive 8 fdc

Examples - Breakpoints and Watchpoints:
  drive 8 bp $80CE
  drive 8 bp list
  drive 8 clearbp all
  drive 8 watch read $1C01
  drive 8 watch write $1800
  drive 8 watch list
  drive 8 clearwatch all

Examples - Dual-CPU Breakpoints (4040):
  drive ieee488 8 bp cpu 0 $D4DC
  drive ieee488 8 bp cpu 1 $FF8C
  drive ieee488 8 bp cpu 1 $FFAA if a $0F
  drive ieee488 8 bp list
  drive ieee488 8 clearbp cpu 1 $FF8C
  drive ieee488 8 clearbp all

Examples - IEC Drives:
  drive iec 8
  drive iec 8 hardware
  drive iec 8 cpu
  drive iec 8 mem $0300 64
  drive iec 8 step
  drive iec 8 cycle

Examples - IEEE-488 Drives:
  drive ieee488 8
  drive ieee488 8 hardware
  drive ieee488 8 cpu
  drive ieee488 8 mem $0300 64
  drive ieee488 8 step
  drive ieee488 8 cycle

Examples - Dual-Mechanism Drives (4040):
  drive ieee488 8 mount 0 "disk1.d64"
  drive ieee488 8 mount 1 "disk2.d64"
  drive ieee488 8 chip 10
  drive ieee488 8 chip 11
  drive ieee488 8 unmount 0
  drive ieee488 8 unmount 1

Examples - Dual-CPU Drives (4040):
  drive ieee488 8 hardware
  drive ieee488 8 cpu 0
  drive ieee488 8 cpu 1
  drive ieee488 8 mem cpu 0 $D000
  drive ieee488 8 mem cpu 1 $1C00
  drive ieee488 8 mem cpu 1 $FF8C
  drive ieee488 8 chip 8
  drive ieee488 8 step 0
  drive ieee488 8 step 1
  drive ieee488 8 cycle 0
  drive ieee488 8 cycle 1

Notes:
  - All drive commands support optional bus selection.
  - CPU IDs and hardware IDs are listed by 'drive <id> hardware'.
  - CPU 0 is selected by default when no CPU ID is specified.
  - Memory access uses the selected CPU's memory bus.
  - Hardware availability depends on the drive model.
  - Unsupported hardware components are reported by the monitor.
  - Breakpoints and watchpoints belong to the selected drive.
  - Use 'iec' for IEC serial-bus diagnostics.
  - Use 'ieee488' for IEEE-488 parallel-bus diagnostics.
  - The 'drive state' command is currently IEC-specific.

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
            std::cout << "  drive " << id << " bp cpu <cpu-id> <address>\n";
            std::cout << "  drive " << id << " bp <address> if <a|x|y> <value>\n";
            std::cout << "  drive " << id << " bp cpu <cpu-id> <address> if <a|x|y> <value>\n";
            std::cout << "  drive " << id << " bp list\n";
            return;
        }

        // List all CPU breakpoints.
        if (args[3] == "list")
        {
            if (args.size() != 4)
            {
                std::cout << "Usage: drive " << id << " bp list\n";
                return;
            }

            backend->dumpDriveBreakpoints(id, busType);
            return;
        }

        try
        {
            uint32_t cpuId = 0;
            size_t addressIndex = 3;

            // Optional CPU selection:
            // drive 8 bp cpu 1 $FF8C
            if (args[3] == "cpu")
            {
                if (args.size() < 6)
                {
                    std::cout << "Usage: drive " << id
                              << " bp cpu <cpu-id> <address>\n";
                    return;
                }

                size_t parsed = 0;

                const unsigned long value =
                    std::stoul(args[4], &parsed, 10);

                if (parsed != args[4].size() ||
                    value > UINT32_MAX)
                {
                    throw std::out_of_range("Invalid CPU ID");
                }

                cpuId = static_cast<uint32_t>(value);
                addressIndex = 5;
            }

            const uint16_t address =
                parseAddress(args[addressIndex]);

            const size_t remaining =
                args.size() - addressIndex - 1;

            // Unconditional breakpoint.
            if (remaining == 0)
            {
                backend->addDriveBreakpoint(
                    id,
                    address,
                    busType,
                    cpuId);

                return;
            }

            // Conditional breakpoint.
            if (remaining == 3 &&
                args[addressIndex + 1] == "if")
            {
                const std::string& reg =
                    args[addressIndex + 2];

                const uint8_t value =
                    static_cast<uint8_t>(
                        parseAddress(args[addressIndex + 3]) & 0xFF);

                Drive::DriveBreakpoint::Condition condition;

                if (reg == "a" || reg == "A")
                {
                    condition =
                        Drive::DriveBreakpoint::Condition::AEquals;
                }
                else if (reg == "x" || reg == "X")
                {
                    condition =
                        Drive::DriveBreakpoint::Condition::XEquals;
                }
                else if (reg == "y" || reg == "Y")
                {
                    condition =
                        Drive::DriveBreakpoint::Condition::YEquals;
                }
                else
                {
                    std::cout << "Invalid breakpoint register: "
                              << reg << "\n";

                    std::cout << "Supported registers: a, x, y\n";
                    return;
                }

                backend->addDriveConditionalBreakpoint(
                    id,
                    address,
                    condition,
                    value,
                    busType,
                    cpuId);

                return;
            }

            std::cout << "Usage:\n";
            std::cout << "  drive " << id << " bp <address>\n";
            std::cout << "  drive " << id
                      << " bp cpu <cpu-id> <address>\n";
            std::cout << "  drive " << id
                      << " bp <address> if <a|x|y> <value>\n";
            std::cout << "  drive " << id
                      << " bp cpu <cpu-id> <address> if <a|x|y> <value>\n";
            std::cout << "  drive " << id << " bp list\n";
        }
        catch (const std::exception& e)
        {
            std::cout << "Invalid breakpoint: "
                      << e.what() << "\n";
        }

        return;
    }

    if (subcmd == "chip")
    {
        if (args.size() < 4)
        {
            std::cout << "Usage: drive <id> chip <hardware-id>\n";
            return;
        }

        try
        {
            size_t parsed = 0;
            const unsigned long value = std::stoul(args[3], &parsed, 10);

            if (parsed != args[3].size() || value > UINT32_MAX)
                throw std::out_of_range("Invalid hardware ID");

            backend->dumpDriveChip(id, static_cast<uint32_t>(value), busType);
        }
        catch (const std::exception&)
        {
            std::cout << "Invalid hardware ID.\n";
        }

        return;
    }

    if (subcmd == "clearbp")
    {
        if (args.size() < 4)
        {
            std::cout << "Usage:\n";
            std::cout << "  drive " << id << " clearbp <address>\n";
            std::cout << "  drive " << id
                      << " clearbp cpu <cpu-id> <address>\n";
            std::cout << "  drive " << id << " clearbp all\n";
            return;
        }

        // Remove all breakpoints belonging to this drive.
        if (args[3] == "all")
        {
            if (args.size() != 4)
            {
                std::cout << "Usage: drive " << id
                          << " clearbp all\n";
                return;
            }

            backend->clearDriveBreakpoints(id, busType);
            return;
        }

        try
        {
            uint32_t cpuId = 0;
            size_t addressIndex = 3;

            // Optional CPU selection.
            if (args[3] == "cpu")
            {
                if (args.size() != 6)
                {
                    std::cout << "Usage: drive " << id
                              << " clearbp cpu <cpu-id> <address>\n";
                    return;
                }

                size_t parsed = 0;

                const unsigned long value =
                    std::stoul(args[4], &parsed, 10);

                if (parsed != args[4].size() || value > UINT32_MAX)
                    throw std::out_of_range("Invalid CPU ID");

                cpuId = static_cast<uint32_t>(value);
                addressIndex = 5;
            }
            else if (args.size() != 4)
            {
                std::cout << "Usage: drive " << id
                          << " clearbp <address>\n";
                return;
            }

            const uint16_t address = parseAddress(args[addressIndex]);

            backend->removeDriveBreakpoint(id, address, busType, cpuId);
        }
        catch (const std::exception& e)
        {
            std::cout << "Invalid breakpoint: "
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

    if (subcmd == "hardware" || subcmd == "hw")
    {
        backend->dumpDriveHardware(id, busType);
        return;
    }

    if (subcmd == "cpu")
    {
        uint32_t cpuId = 0;

        if (args.size() >= 4)
        {
            try
            {
                size_t parsed = 0;
                const unsigned long value = std::stoul(args[3], &parsed, 10);

                if (parsed != args[3].size() || value > UINT32_MAX)
                    throw std::out_of_range("Invalid CPU ID");

                cpuId = static_cast<uint32_t>(value);
            }
            catch (const std::exception&)
            {
                std::cout << "Invalid CPU hardware ID.\n";
                std::cout << "Usage: drive <id> cpu [cpu-id]\n";
                return;
            }
        }

        backend->dumpDriveCPU(id, busType, cpuId);
        return;
    }

    if (subcmd == "mem")
    {
        uint32_t cpuId = 0;
        size_t addressArg = 3;

        // Optional CPU selection:
        // drive 8 mem cpu 1 $1C00
        if (args.size() >= 4 && args[3] == "cpu")
        {
            if (args.size() < 6)
            {
                std::cout << "Usage: drive <id> mem cpu "
                             "<cpu-id> <addr> [count]\n";
                return;
            }

            try
            {
                size_t parsed = 0;

                const unsigned long value =
                    std::stoul(args[4], &parsed, 10);

                if (parsed != args[4].size() ||
                    value > UINT32_MAX)
                {
                    throw std::out_of_range("Invalid CPU ID");
                }

                cpuId = static_cast<uint32_t>(value);
                addressArg = 5;
            }
            catch (const std::exception&)
            {
                std::cout << "Invalid CPU hardware ID.\n";
                return;
            }
        }

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
            const std::string& spec = args[addressArg];

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

                const uint32_t len = static_cast<uint32_t>(b) - static_cast<uint32_t>(a) + 1u;

                if (len == 0 || len > 0xFFFFu)
                    throw std::runtime_error("Range too large");

                count = static_cast<uint16_t>(len);
            }
            else
            {
                start = parseAddress(spec);

                if (args.size() > addressArg + 1)
                {
                    const uint16_t parsedCount = parseAddress(args[addressArg + 1]);

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

        backend->dumpDriveMemory(id, start, count, busType, cpuId);
        return;
    }

    if (subcmd == "cia")
    {
        backend->dumpDriveCIA(id);
        return;
    }

    if (subcmd == "fdc")
    {
        backend->dumpDriveFDC(id, busType);
        return;
    }

    if (subcmd == "state")
    {
        backend->dumpDriveState(id, busType);
        return;
    }

    if (subcmd == "step" || subcmd == "cycle")
    {
        uint32_t cpuId = 0;

        if (args.size() >= 4)
        {
            try
            {
                size_t parsed = 0;

                const unsigned long value = std::stoul(args[3], &parsed, 10);

                if (parsed != args[3].size() || value > UINT32_MAX)
                    throw std::out_of_range("Invalid CPU ID");

                cpuId = static_cast<uint32_t>(value);
            }
            catch (const std::exception&)
            {
                std::cout << "Invalid CPU hardware ID.\n";
                std::cout << "Usage: drive <id> "
                          << subcmd << " [cpu-id]\n";
                return;
            }
        }

        const bool instructionStep = (subcmd == "step");

        backend->driveCPUStep(id, busType, cpuId, instructionStep);

        return;
    }

    if (subcmd == "mount")
    {
        if (args.size() < 5)
        {
            std::cout << "Usage: drive " << id
                      << " mount <mechanism-id> <disk-image-path>\n";
            return;
        }

        try
        {
            // Parse mechanism ID.
            size_t parsed = 0;

            const unsigned long value = std::stoul(args[3], &parsed, 10);

            if (parsed != args[3].size() || value > UINT32_MAX)
                throw std::out_of_range("Invalid mechanism ID");

            const uint32_t mechanismId = static_cast<uint32_t>(value);

            // Reassemble the disk path in case the monitor
            // tokenizer split a path containing spaces.
            std::string path;

            for (size_t i = 4; i < args.size(); ++i)
            {
                if (!path.empty())
                    path += " ";

                path += args[i];
            }

            // Remove surrounding quotes if present.
            if (path.size() >= 2 && path.front() == '"' && path.back() == '"')
                path = path.substr(1, path.size() - 2);

            if (path.empty())
            {
                std::cout << "Disk image path cannot be empty.\n";
                return;
            }

            backend->mountDriveMechanism(id, mechanismId, path, busType);
        }
        catch (const std::exception& e)
        {
            std::cout << "Invalid mount command: "
                      << e.what() << "\n";
        }

        return;
    }

    if (subcmd == "unmount")
    {
        if (args.size() != 4)
        {
            std::cout << "Usage: drive " << id
                      << " unmount <mechanism-id>\n";
            return;
        }

        try
        {
            size_t parsed = 0;

            const unsigned long value = std::stoul(args[3], &parsed, 10);

            if (parsed != args[3].size() || value > UINT32_MAX)
                throw std::out_of_range("Invalid mechanism ID");

            const uint32_t mechanismId = static_cast<uint32_t>(value);

            backend->unmountDriveMechanism(id, mechanismId, busType);
        }
        catch (const std::exception& e)
        {
            std::cout << "Invalid unmount command: "
                      << e.what() << "\n";
        }

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
