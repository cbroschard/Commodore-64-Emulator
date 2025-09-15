// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Computer.h"
#include "Debug/MLMonitor.h"
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
        "    mode          Show current VIC-II graphics mode\n"
        "    banks         Show current screen/charset/bitmap base addresses \n"
        "    regs          Dump VIC-II registers\n";

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
        "  collisions  Sprite collision latches (D01E, D01F)\n"
        "  colors      Border/background/sprite colors (D020-D02E)\n"
        "  pos         Sprite X/Y positions (D000-D00F, D010)\n";
}

void VICCommand::execute(MLMonitor& mon, const std::vector<std::string>& args)
{
    if (args.size() < 2)
    {
        std::cout << help() << std::endl;
        return;
    }

    const std::string& sub = args[1];  // real subcommand

    if (isHelp(sub))
    {
        std::cout << "Usage:\n" << help() << std::endl;
        return;
    }
    else if (sub == "mode")
    {
        std::cout << "Current VIC-II mode: "
                  << mon.computer()->vicGetModeName()
                  << std::endl;
    }
    else if (sub == "banks")
    {
        std::cout << mon.computer()->getCurrentVICBanks() << std::endl;
    }
    else if (sub == "regs")
    {
        if (args.size() == 2)
        {
            std::cout << regsUsage();
        }
        else
        {
            const std::string& group = args[2]; // third word
            static const std::set<std::string> validGroups = {
                "all", "raster", "irq", "sprites", "collisions", "colors", "pos"
            };

            if (validGroups.count(group))
                if (group == "all")
                {
                    printPaged(mon.computer()->vicDumpRegs(group));
                }
                else
                {
                    std::cout << mon.computer()->vicDumpRegs(group) << std::endl;
                }
            else
                std::cout << regsUsage();
        }
    }
    else
    {
        std::cout << "Unknown vic subcommand: " << sub << std::endl;
        std::cout << help() << std::endl;
    }
}
