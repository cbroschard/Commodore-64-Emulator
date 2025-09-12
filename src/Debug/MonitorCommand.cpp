// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "MonitorCommand.h"

MonitorCommand::MonitorCommand() = default;

MonitorCommand::~MonitorCommand() = default;

uint16_t MonitorCommand::parseAddress(const std::string& s)
{
    // Empty input -> invalid
    if (s.empty())
    {
        throw std::invalid_argument("Empty address");
    }

    // If it starts with '$', parse as hex
    if (s[0] == '$')
    {
        return static_cast<uint16_t>(
            std::stoul(s.substr(1), nullptr, 16)
        );
    }

    // If it looks like hex (has [A-Fa-f] letters), treat as hex
    if (s.find_first_of("ABCDEFabcdef") != std::string::npos)
    {
        return static_cast<uint16_t>(
            std::stoul(s, nullptr, 16)
        );
    }

    // Otherwise, decimal
    return static_cast<uint16_t>
    (
        std::stoul(s, nullptr, 10)
    );
}

bool MonitorCommand::isHelp(const std::string& s) const
{
        return (s == "help" || s == "h" || s == "?" ||
                s == "-help" || s == "--help");
}
