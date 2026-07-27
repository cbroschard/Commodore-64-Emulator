// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include "Debug/HistoryCommand.h"
#include "Debug/MLMonitor.h"
#include "Debug/MLMonitorBackend.h"

HistoryCommand::HistoryCommand() = default;

HistoryCommand::~HistoryCommand() = default;

int HistoryCommand::order() const
{
    return 6;
}

std::string HistoryCommand::name() const
{
    return "history";
}

std::string HistoryCommand::category() const
{
    return "CPU/Execution";
}

std::string HistoryCommand::shortHelp() const
{
    return
        "history [count|clear|on|off|status]       "
        "- Show and control execution history";
}

std::string HistoryCommand::help() const
{
    return
        "history - Show recently executed CPU instructions\n"
        "\n"
        "USAGE\n"
        "  history\n"
        "      Show the 20 most recent instructions.\n"
        "  history <count>\n"
        "      Show up to <count> recent instructions.\n"
        "  history clear\n"
        "      Clear all stored execution history.\n"
        "  history on\n"
        "      Enable execution-history recording.\n"
        "  history off\n"
        "      Disable execution-history recording.\n"
        "  history status\n"
        "      Show recording status, size, and capacity.\n"
        "\n"
        "NOTES\n"
        "  - Register values show CPU state before each instruction.\n"
        "  - The newest retained instruction is shown last.\n"
        "  - Once full, the oldest entries are overwritten.\n"
        "\n"
        "EXAMPLES\n"
        "  history\n"
        "  history 50\n"
        "  history clear\n"
        "  history status\n"
        "  history off\n";
}

void HistoryCommand::execute(
    MLMonitor& mon,
    const std::vector<std::string>& args)
{
    MLMonitorBackend* backend = mon.mlmonitorbackend();

    if (backend == nullptr || !backend->hasExecutionHistory())
    {
        std::cout << "Execution history is not available.\n";
        return;
    }

    // The parser includes the command name in args[0].
    //
    // history        -> args = {"history"}
    // history 50     -> args = {"history", "50"}
    // history clear  -> args = {"history", "clear"}

    std::size_t requestedCount = 20;

    if (args.size() >= 2)
    {
        const std::string& sub = args[1];

        if (isHelp(sub))
        {
            std::cout << help() << std::endl;
            return;
        }
        else if (sub == "clear")
        {
            if (args.size() != 2)
            {
                std::cout << "Usage: history clear\n";
                return;
            }

            backend->clearExecutionHistory();

            std::cout << "Execution history cleared.\n";
            return;
        }
        else if (sub == "on")
        {
            if (args.size() != 2)
            {
                std::cout << "Usage: history on\n";
                return;
            }

            backend->setExecutionHistoryEnabled(true);

            std::cout << "Execution history recording enabled.\n";
            return;
        }
        else if (sub == "off")
        {
            if (args.size() != 2)
            {
                std::cout << "Usage: history off\n";
                return;
            }

            backend->setExecutionHistoryEnabled(false);

            std::cout << "Execution history recording disabled.\n";
            return;
        }
        else if (sub == "status")
        {
            if (args.size() != 2)
            {
                std::cout << "Usage: history status\n";
                return;
            }

            std::cout
                << "Execution history: "
                << (backend->isExecutionHistoryEnabled()
                    ? "enabled"
                    : "disabled")
                << "\n";

            std::cout
                << "Entries: "
                << backend->getExecutionHistorySize()
                << " / "
                << backend->getExecutionHistoryCapacity()
                << "\n";

            return;
        }
        else
        {
            if (args.size() != 2)
            {
                std::cout
                    << "Usage: history "
                    << "[count|clear|on|off|status]\n";

                return;
            }

            try
            {
                std::size_t parsedCharacters = 0;

                const unsigned long parsedCount =
                    std::stoul(
                        sub,
                        &parsedCharacters,
                        10);

                if (parsedCharacters != sub.size())
                {
                    std::cout
                        << "Invalid history count: "
                        << sub
                        << "\n";

                    return;
                }

                if (parsedCount == 0)
                {
                    std::cout
                        << "History count must be greater than zero.\n";

                    return;
                }

                requestedCount =
                    static_cast<std::size_t>(parsedCount);
            }
            catch (const std::invalid_argument&)
            {
                std::cout
                    << "Unknown history option or invalid count: "
                    << sub
                    << "\n";

                std::cout << help();
                return;
            }
            catch (const std::out_of_range&)
            {
                std::cout << "History count is too large.\n";
                return;
            }
        }
    }

    if (args.size() > 2)
    {
        std::cout
            << "Usage: history [count|clear|on|off|status]\n";

        return;
    }

    const std::vector<ExecutionHistoryEntry> entries =
        backend->getExecutionHistory(requestedCount);

    if (entries.empty())
    {
        std::cout << "Execution history is empty.\n";
        return;
    }

    std::ostringstream out;

    out << "PC    BYTES       A  X  Y  SP SR  "
        << "CYCLES      RASTER\n";

    out << "----  ---------   -- -- -- -- --  "
        << "----------  -------\n";

    for (const ExecutionHistoryEntry& entry : entries)
    {
        out << std::uppercase
            << std::hex
            << std::setfill('0');

        out << std::setw(4)
            << static_cast<unsigned>(entry.pc)
            << "  ";

        out << std::setw(2)
            << static_cast<unsigned>(entry.opcode)
            << ' ';

        out << std::setw(2)
            << static_cast<unsigned>(entry.operand1)
            << ' ';

        out << std::setw(2)
            << static_cast<unsigned>(entry.operand2)
            << "   ";

        out << std::setw(2)
            << static_cast<unsigned>(entry.a)
            << ' ';

        out << std::setw(2)
            << static_cast<unsigned>(entry.x)
            << ' ';

        out << std::setw(2)
            << static_cast<unsigned>(entry.y)
            << ' ';

        out << std::setw(2)
            << static_cast<unsigned>(entry.sp)
            << ' ';

        out << std::setw(2)
            << static_cast<unsigned>(entry.sr)
            << "  ";

        out << std::dec
            << std::setfill(' ')
            << std::setw(10)
            << entry.totalCycles
            << "  ";

        out << std::setw(3)
            << entry.rasterLine
            << ':'
            << std::setw(3)
            << entry.rasterDot
            << '\n';
    }

    std::cout << out.str();
}
