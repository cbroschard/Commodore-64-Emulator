// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Computer.h"
#include "Debug/MLMonitor.h"
#include "Debug/PLACommand.h"

PLACommand::PLACommand() = default;

PLACommand::~PLACommand() = default;

std::string PLACommand::name() const
{
    return "pla";
}

std::string PLACommand::category() const
{
    return "Chip/PlA";
}

std::string PLACommand::shortHelp() const
{
    return "pla       - Show PLA mode or decode address";
}

std::string PLACommand::help() const
{
    std::ostringstream out;
    out << "pla                Show current PLA mode and memory mapping\n";
    out << "pla $ADDR          Decode a specific address (which chip is mapped)\n";
    return out.str();
}

void PLACommand::execute(MLMonitor& mon, const std::vector<std::string>& args)
{
    if (args.size() == 1)
    {
        std::cout << mon.computer()->plaGetState() << std::endl;
        return;
    }

    if (args.size() > 2 || isHelp(args[1]))
    {
        std::cout << "Usage:\n" << help() << std::endl;
        return;
    }

    if (args.size() == 2)
    {
        try
        {
            uint16_t address = parseAddress(args[1]);
            std::cout << mon.computer()->plaGetAddressInfo(address) << std::endl;
        }
        catch (...)
        {
            std::cout << "Invalid address: " << args[1] << std::endl;
        }
        return;
    }

    // Case 4: too many args
    std::cout << "Usage:\n" << help() << std::endl;
}
