// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "GoCommand.h"
#include "Debug/MLMonitor.h"
#include "Debug/MLMonitorBackend.h"

GoCommand::GoCommand() :
    trapAddress(0xFFF)
{

}

GoCommand::~GoCommand() = default;

int GoCommand::order() const
{
    return 5;
}


std::string GoCommand::name() const
{
    return "g";
}

std::string GoCommand::category() const
{
    return "CPU/Execution";
}

std::string GoCommand::shortHelp() const
{
    return "g [addr] [force|!] - Start execution";
}

std::string GoCommand::help() const
{
    return
        "g - Start execution\n"
        "\n"
        "Usage:\n"
        "  g\n"
        "      Start execution from the current PC.\n"
        "\n"
        "  g <addr>\n"
        "      Set PC to <addr>, then start execution.\n"
        "\n"
        "  g force\n"
        "  g !\n"
        "      Start execution even if monitor-forced IRQ is active.\n"
        "\n"
        "  g <addr> force\n"
        "  g <addr> !\n"
        "      Set PC to <addr>, then start execution even if monitor-forced IRQ is active.\n"
        "\n"
        "Examples:\n"
        "  g\n"
        "  g $C000\n"
        "  g force\n"
        "  g $C000 force\n"
        "  g $C000 !\n";
}

void GoCommand::execute(MLMonitor& mon, const std::vector<std::string>& args)
{
    if (args.size() >= 2 && isHelp(args[1]))
    {
        std::cout << help();
        return;
    }

    if (args.size() > 3)
    {
        std::cout << "Invalid arguments.\n";
        std::cout << "Usage: g [addr] [force|!]\n";
        return;
    }

    MLMonitorBackend* backend = mon.mlmonitorbackend();

    if (backend == nullptr)
    {
        std::cout << "Monitor backend is not attached.\n";
        return;
    }

    bool forceRun = false;
    bool haveAddress = false;
    uint16_t address = 0;

    for (size_t i = 1; i < args.size(); ++i)
    {
        if (args[i] == "force" || args[i] == "!")
        {
            if (forceRun)
            {
                std::cout << "Invalid argument: force specified more than once.\n";
                std::cout << "Usage: g [addr] [force|!]\n";
                return;
            }

            forceRun = true;
            continue;
        }

        if (haveAddress)
        {
            std::cout << "Invalid arguments.\n";
            std::cout << "Usage: g [addr] [force|!]\n";
            return;
        }

        try
        {
            address = parseAddress(args[i]);
            haveAddress = true;
        }
        catch (...)
        {
            std::cout << "Invalid argument: " << args[i] << "\n";
            std::cout << "Usage: g [addr] [force|!]\n";
            return;
        }
    }

    if (backend->irqForceActive() && !forceRun)
    {
        std::cout << "WARNING: monitor IRQ source is still forced active.\n";
        std::cout << "This can cause an IRQ storm, fast cursor flashing, and slow BASIC input.\n\n";
        std::cout << "Use:\n";
        std::cout << "  irq clear\n";
        std::cout << "  g\n\n";
        std::cout << "Or run anyway with:\n";
        std::cout << "  g force\n";
        return;
    }

    if (haveAddress)
        backend->setPC(address);

    mon.setRunningFlag(false);
}
