// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Computer.h"
#include "Debug/LogCommand.h"
#include "Debug/MLMonitor.h"

struct LogCommand::Logged
{
    LogSet logtype;
    bool enabled;
};

LogCommand::LogCommand()
{
    logStatus = {
        { LogSet::Cartridge, false },
        { LogSet::Cassette, false},
        { LogSet::CIA1, false},
        { LogSet::CIA2, false},
        { LogSet::CPU, false},
        { LogSet::IO, false},
        { LogSet::Joystick, false},
        { LogSet::Keyboard, false},
        { LogSet::Memory, false},
        { LogSet::PLA, false},
        { LogSet::VIC, false}
    };
}

LogCommand::~LogCommand() = default;

int LogCommand::order() const
{
    return 5;
}

std::string LogCommand::category() const
{
    return "Debugging";
}

std::string LogCommand::name () const
{
    return "log";
}

std::string LogCommand::shortHelp() const
{
    return "log <type> <enable|disable|status> - control or query per-component logging";
}

std::string LogCommand::help() const
{
    return R"(Log command - enable, disable, or check logging for components.

USAGE
  log <type> <enable|disable|status>
  log status

TYPES
  Cartridge, Cassette, CIA1, CIA2, CPU, IO,
  Joystick, Keyboard, Memory, PLA, VIC

ACTIONS
  enable   Start logging for the component.
  disable  Stop logging for the component.
  status   With a <type>: show that component’s state.
           Without a <type> (just "log status"): show all components’ states.

NOTES
  Type and action are case-insensitive.

EXAMPLES
  log cpu enable
  log vic disable
  log keyboard status
  log status
)";
}

void LogCommand::execute(MLMonitor& mon, const std::vector<std::string>& args)
{
    if (args.size() < 2 || isHelp(args[1]))
    {
        std::cout << help();
        return;
    }

    // Case 1: "log status" -> show all
    if (args[1] == "status")
    {
        for (const auto& entry : logStatus)
        {
            std::cout << logSetToString(entry.logtype)
                      << " logging is "
                      << stateString(entry.enabled)
                      << "\n";
        }
        return;
    }

    // Case 2: "log <type> <action>"
    auto logSetOpt = stringToLogSet(args[1]);
    if (!logSetOpt)
    {
        std::cout << "Unknown log type: " << args[1] << "\n";
        return;
    }

    if (args.size() < 3)
    {
        std::cout << "Missing action (enable|disable|status)\n";
        return;
    }

    if (args[2] == "status")
    {
        for (const auto& entry : logStatus)
        {
            if (entry.logtype == *logSetOpt)
            {
                std::cout << logSetToString(entry.logtype)
                          << " logging is "
                          << stateString(entry.enabled)
                          << "\n";
            }
        }
    }
    else if (args[2] == "enable" || args[2] == "disable")
    {
        bool enable = (args[2] == "enable");
        for (auto& entry : logStatus)
        {
            if (entry.logtype == *logSetOpt)
            {
                entry.enabled = enable;
                // Update computer to tell the class to enable logging
                mon.computer()->setLogging(entry.logtype, enable);
                std::cout << logSetToString(entry.logtype)
                          << " logging "
                          << stateString(enable)
                          << "\n";
            }
        }
    }
    else
    {
        std::cout << "Invalid action: " << args[2] << "\n";
    }
}

std::optional<LogSet> LogCommand::stringToLogSet(const std::string& name)
{
    // Convert to lower before checking
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower == "cartridge") return LogSet::Cartridge;
    else if (lower == "cassette") return LogSet::Cassette;
    else if (lower == "cia1") return LogSet::CIA1;
    else if (lower == "cia2") return LogSet::CIA2;
    else if (lower == "cpu") return LogSet::CPU;
    else if (lower == "io") return LogSet::IO;
    else if (lower == "joystick") return LogSet::Joystick;
    else if (lower == "keyboard") return LogSet::Keyboard;
    else if (lower == "memory") return LogSet::Memory;
    else if (lower == "pla") return LogSet::PLA;
    else if (lower == "vic") return LogSet::VIC;

    return std::nullopt;
}

std::string LogCommand::logSetToString(LogSet type)
{
    switch (type)
    {
        case LogSet::Cartridge: return "Cartridge";
        case LogSet::Cassette: return "Cassette";
        case LogSet::CIA1: return "CIA1";
        case LogSet::CIA2: return "CIA2";
        case LogSet::CPU: return "CPU";
        case LogSet::IO: return "IO";
        case LogSet::Joystick: return "Joystick";
        case LogSet::Keyboard: return "Keyboard";
        case LogSet::Memory: return "Memory";
        case LogSet::PLA: return "PLA";
        case LogSet::VIC: return "VIC";
    }

    // Default not found
    return "Unknown";
}

bool LogCommand::setLogEnabled(std::vector<Logged>& logs, LogSet type, bool enabled)
{
    for (auto& entry : logs)
    {
        if (entry.logtype == type)
        {
            entry.enabled = enabled;
            return true; // found and updated
        }
    }
    return false; // not found
}

std::string LogCommand::stateString(bool enabled)
{
    return enabled ? "ENABLED" : "DISABLED";
}
