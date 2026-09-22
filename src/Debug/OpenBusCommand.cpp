// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Debug/OpenBusCommand.h"
#include "Debug/MLMonitor.h"
#include "Debug/MLMonitorBackend.h"

OpenBusCommand::OpenBusCommand()
{

}

OpenBusCommand::~OpenBusCommand() = default;

int OpenBusCommand::order() const
{
    return 1;
}

std::string OpenBusCommand::category() const
{
    return "System";
}

std::string OpenBusCommand::name() const
{
    return "openbus";
}

std::string OpenBusCommand::shortHelp() const
{
    return "openbus - Displays shared data-bus latch and open-bus diagnostics.";
}

std::string OpenBusCommand::help() const
{
    return
        "Usage:\n"
        "  openbus status\n"
        "  openbus decay\n"
        "  openbus decay clear\n"
        "\n"
        "Displays the current shared C64 data-bus latch state and\n"
        "diagnostic information used for open-bus behavior.\n"
        "\n"
        "Options:\n"
        "  openbus status\n"
        "      Show the current latched value, last bus driver,\n"
        "      and last update cycle.\n"
        "\n"
        "  openbus decay\n"
        "      Show per-bit data-bus decay diagnostics, including\n"
        "      the last driven cycle and maximum observed age.\n"
        "\n"
        "  openbus decay clear\n"
        "      Clear the collected decay diagnostics without\n"
        "      modifying the current data-bus latch state.\n";
}

void OpenBusCommand::execute(MLMonitor& mon, const std::vector<std::string>& args)
{
    if (args.size() < 2 || args.size() > 3)
    {
        std::cout << help() << std::endl;
        return;
    }

    const std::string& sub = args[1];

    if (isHelp(sub))
    {
        std::cout << help() << std::endl;
        return;
    }
    else if (sub == "status")
    {
        const auto driver = mon.mlmonitorbackend()->getDataBusLatchLastDriver();

        std::cout
            << "Open Bus Status\n"
            << "---------------\n"
            << "Last Driver:      "
            << mon.mlmonitorbackend()->dataBusLatchDriverToString(driver)
            << "\n"
            << "Last Update Cycle: "
            << mon.mlmonitorbackend()->getDataBusLatchLastUpdateCycle()
            << "\n";

        return;
    }
    else if (sub == "decay")
    {
        if (args.size() == 2)
        {
            const uint64_t* maxAge = mon.mlmonitorbackend()->getDataBusLatchMaxObservedAge();

            if (!maxAge)
            {
                std::cout << "DataBusLatch not attached.\n";
                return;
            }

            std::cout << "Open Bus Decay Diagnostics\n"  << "--------------------------\n";

            for (int bit = 0; bit < 8; ++bit)
            {
                std::cout << "D" << bit << "  Last Driven Cycle: " << mon.mlmonitorbackend()->getDataBusLatchLastDrivenCycle(bit)
                    << "  Max Observed Age: " << maxAge[bit] << " cycles\n";
            }

            return;
        }
        else if (args[2] == "clear")
        {
            mon.mlmonitorbackend()->dataBusLatchClearDiagnostics();
            std::cout << "Open-bus decay diagnostics cleared.\n";
            return;
        }
        else
        {
            std::cout << help() << std::endl;
            return;
        }
    }
    else
    {
        std::cout << "Unknown openbus subcommand: " << sub << "\n";
        std::cout << help() << std::endl;
        return;
    }
}
