// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Computer.h"
#include "Debug/MemoryEditCommand.h"
#include "Debug/MLMonitor.h"
#include "Debug/MLMonitorBackend.h"

MemoryEditCommand::MemoryEditCommand() = default;

MemoryEditCommand::~MemoryEditCommand() = default;

std::string MemoryEditCommand::name() const
{
    return "e";
}

std::string MemoryEditCommand::category() const
{
    return "Memory";
}

std::string MemoryEditCommand::shortHelp() const
{
    return "e         - Edit memory";
}

std::string MemoryEditCommand::help() const
{
    return "e <addr> [value]   - Edit memory at $addr";

}

void MemoryEditCommand::execute(MLMonitor& mon, const std::vector<std::string>& args)
{
    if (args.size() < 3 || isHelp(args[1]))
    {
        std::cout << "Usage: " << help() << std::endl;
        return;
    }
    try
    {
        uint16_t address = parseAddress(args[1]);
        uint8_t value = parseAddress(args[2]);
        mon.mlmonitorbackend()->writeRAM(address, value);
    }
    catch(const std::exception& e)
    {
        std::cout << "Error: Invalid address or value. Usage: " << help() << std::endl;
    }
}
