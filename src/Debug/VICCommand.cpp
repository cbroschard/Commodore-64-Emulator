// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Computer.h"
#include "Debug/MLMonitor.h"
#include "Debug/MLMonitorBackend.h"
#include "Debug/VICCommand.h"

VICCommand::VICCommand() = default;

VICCommand::~VICCommand() = default;

std::string VICCommand::name() const
{
    return "vic";
}

std::string VICCommand::category() const
{
    return "Chip/VIC-II";
}

std::string VICCommand::shortHelp() const
{
    return "vic       - VIC-II operations (use 'vic help')";
}

std::string VICCommand::help() const
{
    return
        "vic <subcommand>:\n"
        "    mode                 Show current VIC-II graphics mode\n"
        "    banks                Show current screen/charset/bitmap base addresses\n"
        "    border               Dump current border state\n"
        "    regs <group>         Dump VIC-II registers\n"
        "    cycle                Show debug info for current raster/cycle\n"
        "    cycle <r> <c>        Show debug info for specific raster/cycle\n"
        "    map <r>              Show fetch map for one raster line\n"
        "    row                  Show badline row sequencer\n";
}

std::string VICCommand::regsUsage() const
{
    return
        "Usage: vic regs [subcommand]\n"
        "Subcommands:\n"
        "  all         Show all registers (default)\n"
        "  raster      Raster/control registers (D011, D012, D016, D018)\n"
        "  irq         Interrupt registers (D019, D01A)\n"
        "  sprites     Sprite control (D015, D017, D01B-D01D)\n"
        "  latch       Latched registers\n"
        "  collisions  Sprite collision latches (D01E, D01F)\n"
        "  colors      Border/background/sprite colors (D020-D02E)\n"
        "  pos         Sprite X/Y positions (D000-D00F, D010)\n";
}

std::string VICCommand::borderUsage() const
{
    return
        "Usage:\n"
        " vic border\n"
        " vic border edge\n"
        " vic border edge <raster>\n";
}

std::string VICCommand::cycleUsage() const
{
    return
        "Usage:\n"
        "  vic cycle\n"
        "  vic cycle <raster> <cycle>\n";
}

std::string VICCommand::mapUsage() const
{
    return
        "Usage:\n"
        "  vic map <raster>\n";
}

void VICCommand::execute(MLMonitor& mon, const std::vector<std::string>& args)
{
    if (args.size() < 2)
    {
        std::cout << help() << std::endl;
        return;
    }

    const std::string& sub = args[1];

    if (isHelp(sub))
    {
        std::cout << "Usage:\n" << help() << std::endl;
        return;
    }
    else if (sub == "mode")
    {
        std::cout << "Current VIC-II mode: "
                  << mon.mlmonitorbackend()->vicGetModeName()
                  << std::endl;
    }
    else if (sub == "banks")
    {
        std::cout << mon.mlmonitorbackend()->getCurrentVICBanks();
    }
    else if (sub == "border")
    {
        if (args.size() == 2)
        {
            std::cout << mon.mlmonitorbackend()->vicDumpBorderState();
        }
        else if (args[2] == "edge")
        {
            if (args.size() == 3)
            {
                std::cout << mon.mlmonitorbackend()->vicDumpBorderWindowAroundCurrentRaster();
            }
            else if (args.size() == 4)
            {
                try
                {
                    const int raster = std::stoi(args[3]);
                    std::cout << mon.mlmonitorbackend()->vicDumpBorderWindowAroundRaster(raster);
                }
                catch (const std::exception&)
                {
                    std::cout << borderUsage();
                }
            }
        }
        else
        {
            std::cout << borderUsage();
        }
    }
    else if (sub == "regs")
    {
        if (args.size() == 2)
        {
            std::cout << regsUsage();
        }
        else
        {
            const std::string& group = args[2];
            static const std::set<std::string> validGroups = {
                "all", "raster", "irq", "sprites", "latch", "collisions", "colors", "pos"
            };

            if (!validGroups.count(group))
            {
                std::cout << regsUsage();
                return;
            }

            const std::string output = mon.mlmonitorbackend()->vicDumpRegs(group);
            std::cout << output;
            if (!output.empty() && output.back() != '\n')
                std::cout << '\n';
        }
    }
    else if (sub == "cycle")
    {
        if (args.size() == 2)
        {
            const std::string output = mon.mlmonitorbackend()->vicDumpCurrentCycleDebug();
            std::cout << output;
            if (!output.empty() && output.back() != '\n')
                std::cout << '\n';
        }
        else if (args.size() == 4)
        {
            try
            {
                const int raster = std::stoi(args[2]);
                const int cycle  = std::stoi(args[3]);

                const std::string output =
                    mon.mlmonitorbackend()->vicDumpCycleDebugFor(raster, cycle);

                std::cout << output;
                if (!output.empty() && output.back() != '\n')
                    std::cout << '\n';
            }
            catch (const std::exception&)
            {
                std::cout << cycleUsage();
            }
        }
        else
        {
            std::cout << cycleUsage();
        }
    }
    else if (sub == "map")
    {
        if (args.size() != 3)
        {
            std::cout << mapUsage();
            return;
        }

        try
        {
            const int raster = std::stoi(args[2]);

            const std::string output =
                mon.mlmonitorbackend()->vicDumpRasterFetchMap(raster);

            std::cout << output;
            if (!output.empty() && output.back() != '\n')
                std::cout << '\n';
        }
        catch (const std::exception&)
        {
            std::cout << mapUsage();
        }
    }
    else if (sub == "row")
    {
        std::cout << mon.mlmonitorbackend()->vicDumpBadlineState();
    }
    else
    {
        std::cout << "Unknown vic subcommand: " << sub << std::endl;
        std::cout << help() << std::endl;
    }
}
