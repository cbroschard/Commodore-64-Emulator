// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Computer.h"
#include "Debug/MLMonitor.h"
#include "Debug/TraceCommand.h"

TraceCommand::TraceCommand() :
    traceMgr(nullptr)
{

}

TraceCommand::~TraceCommand() = default;

int TraceCommand::order() const
{
    return 30;
}

std::string TraceCommand::name() const
{
    return "trace";
}

std::string TraceCommand::category() const
{
    return "CPU/Execution";
}

std::string TraceCommand::shortHelp() const
{
    return "trace     - Enable or control component tracing";
}

std::string TraceCommand::help() const
{
    return
        "Trace - control tracing output and categories\n"
        "\n"
        "Usage:\n"
        "  trace                      Show global trace status (ON/OFF)\n"
        "  trace on|off               Enable or disable tracing globally\n"
        "  trace cats|categories      List all categories and their status\n"
        "  trace dump                 Dump the current trace buffer to console\n"
        "  trace clear                Clear stored trace data\n"
        "  trace file <path>          Write trace output to a file\n"
        "\n"
        "Cartridge tracing:\n"
        "  trace cart enable          Enable Cartridge tracing\n"
        "  trace cart disable         Disable Cartridge tracing\n"
        "\n"
        "CIA tracing:\n"
        " trace cia1 enable           Enable CIA1 tracing\n"
        " trace cia1 disable          Disable CIA1 tracing\n"
        " trace cia2 enable           Enable CIA2 tracing\n"
        " trace cia2 disable          Disable CIA2 tracing\n"
        "\n"
        "CPU tracing:\n"
        "  trace cpu enable           Enable CPU tracing\n"
        "  trace cpu disable          Disable CPU tracing\n"
        "\n"
        "Memory range tracing:\n"
        "  trace mem enable           Enable memory tracing (requires ranges)\n"
        "  trace mem disable          Disable memory tracing\n"
        "  trace mem add <lo>-<hi>    Add a traced address range (hex, inclusive)\n"
        "  trace mem list             List currently traced memory ranges\n"
        "  trace mem clear            Clear all traced memory ranges\n"
        "\n"
        "PLA tracing:\n"
        "  trace pla enable           Enable PLA tracing\n"
        "  trace pla disable          Disable PLA tracing\n"
        "\n"
        "SID tracing:\n"
        "  trace sid enable           Enable SID tracing\n"
        "  trace sid disable          Disable SID tracing\n"
        "\n"
        "Notes:\n"
        "  - Addresses use $HHHH hex notation, e.g. $0800-$0FFF.\n"
        "  - 'trace mem add' does NOT enable the MEM category; use 'trace mem enable'.\n"
        "  - Global tracing must be ON for output: use 'trace on'.\n"
        "\n"
        "Examples:\n"
        "  trace on\n"
        "  trace cats\n"
        "  trace file traces.txt\n"
        "  trace cpu enable\n"
        "  trace mem add $0800-$0FFF\n"
        "  trace mem enable\n"
        "  trace sid enable\n"
        "  trace dump\n";
}

void TraceCommand::execute(MLMonitor& mon, const std::vector<std::string>& args)
{
    if (!traceMgr)
    {
        std::cout << "Trace manager not available.\n";
        return;
    }

    // No subcommand => show status
    if (args.size() == 1)
    {
        std::cout << "Trace " << (traceMgr->isEnabled() ? "ON" : "OFF") << "\n";
        return;
    }

    const std::string& sub = args[1];

    if (isHelp(sub))
    {
        std::cout << help();
        return;
    }

    if (sub == "on")
    {
        traceMgr->enable(true);
        std::cout << "Tracing enabled.\n";
        return;
    }

    if (sub == "off")
    {
        traceMgr->enable(false);
        std::cout << "Tracing disabled.\n";
        return;
    }
    if (sub == "cats" || sub == "categories")
    {
        std::cout << traceMgr->listCategoryStatus() << "\n";
        return;
    }
    if (sub == "dump")
    {
        traceMgr->dumpBuffer();
        return;
    }
    if (sub == "clear")
    {
        traceMgr->clearBuffer();
        std::cout << "Trace buffer cleared.\n";
        return;
    }
    if (sub == "file")
    {
        if (args.size() < 3)
        {
            std::cout << "Usage: trace file <path>\n";
            return;
        }
        const std::string path = joinArgs(args, 2);
        traceMgr->setFileOutput(path);
        std::cout << "Trace file set to " << path << "\n";
        return;
    }
    if (sub == "cart")
    {
        if (args.size() >= 3 && args[2] == "enable")
        {
            traceMgr->enableCategory(TraceManager::TraceCat::CART);
            std::cout << "Enabled Cartridge tracing." << "\n";
            if (!traceMgr->isEnabled())
            {
                std::cout << "Tracing is not turned on, when ready to activate run: trace on\n";
            }
            return;
        }
        else if (args.size() >= 3 && args[2] == "disable")
        {
            traceMgr->disableCategory(TraceManager::TraceCat::CART);
            std::cout << "Disabled Cartridge tracing." << "\n";
            return;
        }
    }
    if (sub == "cia1")
    {
        if (args.size() >= 3 && args[2] == "enable")
        {
            traceMgr->enableCategory(TraceManager::TraceCat::CIA1);
            std::cout << "Enabled Cartridge tracing." << "\n";
            if (!traceMgr->isEnabled())
            {
                std::cout << "Tracing is not turned on, when ready to activate run: trace on\n";
            }
            return;
        }
        else if (args.size() >= 3 && args[2] == "disable")
        {
            traceMgr->disableCategory(TraceManager::TraceCat::CIA1);
            std::cout << "Disabled Cartridge tracing." << "\n";
            return;
        }
    }
    if (sub == "cia2")
    {
        if (args.size() >= 3 && args[2] == "enable")
        {
            traceMgr->enableCategory(TraceManager::TraceCat::CIA2);
            std::cout << "Enabled Cartridge tracing." << "\n";
            if (!traceMgr->isEnabled())
            {
                std::cout << "Tracing is not turned on, when ready to activate run: trace on\n";
            }
            return;
        }
        else if (args.size() >= 3 && args[2] == "disable")
        {
            traceMgr->disableCategory(TraceManager::TraceCat::CIA2);
            std::cout << "Disabled Cartridge tracing." << "\n";
            return;
        }
    }
    if (sub == "cpu")
    {
        if (args.size() >= 3 && args[2] == "enable")
        {
            traceMgr->enableCategory(TraceManager::TraceCat::CPU);
            std::cout << "Enabled CPU tracing." << "\n";
            if (!traceMgr->isEnabled())
            {
                std::cout << "Tracing is not turned on, when ready to activate run: trace on\n";
            }
            return;
        }
        else if (args.size() >= 3 && args[2] == "disable")
        {
            traceMgr->disableCategory(TraceManager::TraceCat::CPU);
            std::cout << "Disabled CPU tracing" << "\n";
            if (traceMgr->isEnabled())
            {
                std::cout << "Tracing is still enabled globally, if you would like to turn it off run: trace off\n";
            }
            return;
        }
    }
    if (sub == "mem")
    {
        // trace mem add <range>
        if (args.size() >= 3 && args[2] == "enable")
        {
            traceMgr->catOn(TraceManager::TraceCat::MEM);
            if (traceMgr->listMemRange() == "")
            {
                std::cout << "Error: No ranges are added, disabling MEM tracing\n";
                return;
            }
            else if (!traceMgr->isEnabled() && traceMgr->catOn(TraceManager::TraceCat::MEM))
            {
                std::cout << "Tracing is not turned on, when ready to activate run: trace on\n";
                return;
            }
            else
            {
                traceMgr->enableCategory(TraceManager::TraceCat::MEM);
                std::cout << "Enabled Memory tracing\n";
                if (!traceMgr->isEnabled())
                {
                    std::cout << "Tracing is not turned on, when ready to activate run: trace on\n";
                }
                return;
            }
        }
        if (args.size() >= 3 && args[2] == "disable")
        {
            traceMgr->disableCategory(TraceManager::TraceCat::MEM);
            std::cout << "Disabled Memory tracing\n";
            if (traceMgr->isEnabled())
            {
                std::cout << "Tracing is still enabled globally, if you would like to turn it off run: trace off\n";
            }
            return;
        }
        if (args.size() >= 4 && args[2] == "add")
        {
            try
            {
                const std::string rangeStr = joinArgs(args, 3);
                auto [lo, hi] = parseRangePair(rangeStr);
                traceMgr->addMemRange(lo, hi);
                std::cout << "Watching $" << std::hex << std::uppercase
                          << std::setw(4) << std::setfill('0') << lo
                          << "-$" << std::setw(4) << hi << std::dec << "\n";
            }
            catch (const std::exception& e)
            {
                std::cout << "Error: " << e.what() << "\n";
            }
            return;
        }
        else if (args.size() >= 3 && args[2] == "list")
        {
            std::cout << "Memory range: " << traceMgr->listMemRange() << "\n";
            if (!traceMgr->isEnabled())
            {
                std::cout << "Tracing is not turned on, when ready to activate run: trace on\n";
            }
            return;
        }
        else if (args.size() >= 3 && args[2] == "clear")
        {
            traceMgr->clearMemRanges();
            if (traceMgr->isEnabled())
            {
                std::cout << "Tracing is still enabled globally, if you would like to turn it off run: trace off\n";
            }
            return;
        }
    }
    if (sub == "pla")
    {
        if (args.size() >= 3 && args[2] == "enable")
        {
            traceMgr->enableCategory(TraceManager::TraceCat::PLA);
            std::cout << "Enabled PLA tracing.\n";
            if (!traceMgr->isEnabled())
            {
                std::cout << "racing is not turned on, when ready to activate run: trace on\n";
            }
            return;
        }
        else if (args.size() >= 3 && args[2] == "disable")
        {
            traceMgr->disableCategory(TraceManager::TraceCat::PLA);
            std::cout << "Disabled PLA tracing.\n";
            if (traceMgr->isEnabled())
            {
                std::cout << "Tracing is still enabled globally, if you would like to turn it off run: trace off\n";
            }
            return;
        }
    }
    if (sub == "sid")
    {
        if (args.size() >= 3 && args[2] == "enable")
        {
            traceMgr->enableCategory(TraceManager::TraceCat::SID);
            std::cout << "Enabled SID tracing.\n";
            if (!traceMgr->isEnabled())
            {
                std::cout << "Tracing is not turned on, when ready to activate run: trace on\n";
            }
            return;
        }
        else if (args.size() >= 3 && args[2] == "disable")
        {
            traceMgr->disableCategory(TraceManager::TraceCat::SID);
            std::cout << "Disabled SID tracing" << "\n";
            if (traceMgr->isEnabled())
            {
                std::cout << "Tracing is still enabled globally, if you would like to turn it off run: trace off\n";
            }
            return;
        }
    }

    // Unknown subcommand
    std::cout << help();
}

std::string TraceCommand::joinArgs(const std::vector<std::string>& a, size_t start)
{
    std::string s;
    for (size_t i = start; i < a.size(); ++i)
    {
        if (i > start) s += " ";
        s += a[i];
    }
    return s;
}
