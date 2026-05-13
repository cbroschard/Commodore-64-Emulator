// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Debug/MLMonitorBackend.h"
#include "REUCommand.h"

REUCommand::REUCommand()
{

}

REUCommand::~REUCommand() = default;

int REUCommand::order() const
{
    return 5;
}

std::string REUCommand::name() const
{
    return "reu";
}

std::string REUCommand::category() const
{
    return "Hardware/REU";
}

std::string REUCommand::shortHelp() const
{
    return "reu       - REU status, registers, and RAM dump";
}

std::string REUCommand::help() const
{
    return R"(
reu - Inspect RAM Expansion Unit

Usage:
  reu                              Show REU attachment, model, size, and decoded state
  reu clear                        Clear all REU RAM
  reu dump <addr> [count]          Dump REU RAM at REU address
  reu fill <addr> <count> <value>  Fill a range of REU RAM
  reu IRQ                          Dump IRQ status
  reu peek <addr>                  Dump one byte from REU RAM
  reu poke <addr> <value>          Update one byte in REU RAM
  reu regs                         Show compact $DF00-$DF0A register values
  reu test                         Run a built-in self-test that does a small store/fetch/verify cycle using a safe C64 RAM address
  )";
}

void REUCommand::execute(MLMonitor& mon, const std::vector<std::string>& args)
{
    if (args.size() == 1)
    {
        std::cout << mon.mlmonitorbackend()->reuDumpStatus();
        return;
    }

    else if (args.size() == 2 && isHelp(args[1]))
    {
        std::cout << help();
        return;
    }

    const std::string subcmd = args[1];
    if (subcmd == "clear")
    {
        if (args.size() != 3 || args[2] != "confirm")
        {
            std::cout << "This will clear all REU RAM. Use: reu clear confirm\n";
            return;
        }
        else
        {
            std::cout << mon.mlmonitorbackend()->reuClearRAM();
            return;
        }
    }

    else if (subcmd == "dump" && (args.size() == 3 || args.size() == 4))
    {
        try
        {
            uint32_t address = parseAddress32(args[2]);
            uint32_t count = args.size() == 4 ? parseAddress32(args[3]) : 16;

            std::cout << mon.mlmonitorbackend()->reuDumpRAM(address, count);
            return;
        }
        catch (const std::exception& e)
        {
            std::cout << "Invalid address or count!\n";
            std::cout << help();
            return;
        }
    }

    else if (subcmd == "fill" && args.size() == 5)
    {
        try
        {
            uint32_t address = parseAddress32(args[2]);
            uint32_t count = parseAddress32(args[3]);
            uint32_t parsedValue = parseAddress32(args[4]);

            if (parsedValue > 0xFF)
            {
                std::cout << "Value must be 00-FF.\n";
                return;
            }

            uint8_t value = static_cast<uint8_t>(parsedValue);

            std::cout << mon.mlmonitorbackend()->reuFillRAM(address, count, value);
            return;
        }
        catch(const std::exception& e)
        {
            std::cout << "Invalid argument!\n";
            std::cout << help();
            return;
        }
    }

    else if (subcmd == "irq" && args.size() == 2)
    {
        std::cout << mon.mlmonitorbackend()->reuIRQ();
        return;
    }

    else if (subcmd == "peek" && args.size() == 3)
    {
       try
        {
            uint32_t address = parseAddress32(args[2]);

            std::cout << mon.mlmonitorbackend()->reuPeekRAM(address);
            return;
        }
        catch(const std::exception& e)
        {
            std::cout << "Invalid argument!\n";
            std::cout << help();
            return;
        }
    }

    else if (subcmd == "poke" && args.size() == 4)
    {
        try
        {
            uint32_t address = parseAddress32(args[2]);
            uint32_t parsedValue = parseAddress32(args[3]);
            if (parsedValue > 0xFF)
            {
                std::cout << "Value must be 00-FF.\n";
                return;
            }

            uint8_t value = static_cast<uint8_t>(parsedValue);


            std::cout << mon.mlmonitorbackend()->reuPokeRAM(address, value);
            return;
        }
        catch(const std::exception& e)
        {
            std::cout << "Invalid argument!\n";
            std::cout << help();
            return;
        }
    }

    else if (subcmd == "regs" && args.size() == 2)
    {
        std::cout << mon.mlmonitorbackend()->reuDumpRegs();
        return;
    }

    else if (subcmd == "test" && args.size() == 2)
    {
        std::cout << mon.mlmonitorbackend()->reuSelfTest();
        return;
    }

    else
    {
        std::cout << help();
        return;
    }
}
