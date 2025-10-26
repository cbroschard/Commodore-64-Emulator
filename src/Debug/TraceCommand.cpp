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
    return "trace     - Enable or control CPU instruction tracing";
}

std::string TraceCommand::help() const
{
    return "Usage:\n"
           "  trace on|off        - Enable or disable tracing\n"
           "  trace file <path>   - Write traces to a file\n"
           "  trace dump          - Dump trace buffer to console\n"
           "  trace clear         - Clear stored trace data\n";
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
