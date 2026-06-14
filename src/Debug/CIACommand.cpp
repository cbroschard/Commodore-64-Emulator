// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Debug/CIACommand.h"
#include "Debug/MLMonitor.h"
#include "Debug/MLMonitorBackend.h"

CIACommand::CIACommand() = default;

CIACommand::~CIACommand() = default;

int CIACommand::order() const
{
    return 5;
}

std::string CIACommand::name() const
{
    return "cia";
}

std::string CIACommand::category() const
{
    return "Chip/CIA";
}

std::string CIACommand::shortHelp() const
{
    return "cia <1|2> <subcmd> - Inspect CIA1/CIA2 (all, regs, ports, timers, tod, icr, serial, mode/vic/iec)";
}

std::string CIACommand::help() const
{
    return R"(cia - Inspect CIA1 or CIA2

Usage:
    cia <1|2> <subcommand>

Common subcommands:
    all       - Show the full decoded CIA register dump
    regs      - Show the full decoded CIA register dump
    ports     - Show Port A/B values, DDRs, inputs, and outputs
    timers    - Show Timer A and Timer B state
    tod       - Show time-of-day clock and alarm/latch state
    icr       - Show interrupt control register, enable mask, and pending sources
    serial    - Show serial data register / shift-register state
    help      - Show this help text

CIA1-only subcommands:
    mode      - Show CIA1 keyboard/joystick/cassette related mode state

CIA2-only subcommands:
    vic       - Show VIC-II bank select state from CIA2 Port A bits 0-1
    iec       - Show IEC bus snapshot and decoded CIA2 IEC serial bus state

Examples:
    cia 1 all        Show full CIA1 decoded register dump
    cia 1 ports      Show CIA1 Port A/B and DDR state
    cia 1 timers     Show CIA1 Timer A/B values
    cia 1 serial     Show CIA1 serial register state
    cia 1 mode       Show CIA1 keyboard/joystick/cassette mode state

    cia 2 all        Show full CIA2 decoded register dump
    cia 2 ports      Show CIA2 Port A/B and DDR state
    cia 2 timers     Show CIA2 Timer A/B values
    cia 2 serial     Show CIA2 serial register state
    cia 2 vic        Show CIA2 VIC-II bank select state
    cia 2 iec        Show CIA2 IEC serial bus state
)";
}

void CIACommand::execute(MLMonitor& mon, const std::vector<std::string>& args)
{
    // cia help
    // cia ?
    if (args.size() >= 2 && (args[1] == "help" || args[1] == "?"))
    {
        std::cout << help();
        return;
    }

    if (args.size() < 3)
    {
        std::cout << "Usage: cia <1|2> <subcommand>\n";
        std::cout << "Try: cia help\n";
        return;
    }

    int chipNum = 0;

    try
    {
        chipNum = std::stoi(args[1]);
    }
    catch (...)
    {
        std::cout << "Error: first argument must be 1 or 2\n";
        std::cout << "Try: cia help\n";
        return;
    }

    const std::string& subcmd = args[2];

    // cia 1 help
    // cia 2 help
    // cia 1 ?
    // cia 2 ?
    if (subcmd == "help" || subcmd == "?")
    {
        std::cout << help();
        return;
    }

    MLMonitorBackend* backend = mon.mlmonitorbackend();

    if (backend == nullptr)
    {
        std::cout << "Monitor backend is not attached.\n";
        return;
    }

    if (chipNum == 1)
    {
        if (subcmd == "regs" || subcmd == "all")
        {
            std::cout << backend->dumpCIA1Regs();
        }
        else if (subcmd == "ports")
        {
            std::cout << backend->dumpCIA1Ports();
        }
        else if (subcmd == "timers")
        {
            std::cout << backend->dumpCIA1Timers();
        }
        else if (subcmd == "tod")
        {
            std::cout << backend->dumpCIA1TOD();
        }
        else if (subcmd == "icr")
        {
            std::cout << backend->dumpCIA1ICR();
        }
        else if (subcmd == "serial")
        {
            std::cout << backend->dumpCIA1Serial();
        }
        else if (subcmd == "mode")
        {
            std::cout << backend->dumpCIA1Mode();
        }
        else
        {
            std::cout << "Unknown CIA1 subcommand: " << subcmd << "\n";
            std::cout << "Try: cia help\n";
        }
    }
    else if (chipNum == 2)
    {
        if (subcmd == "regs" || subcmd == "all")
        {
            std::cout << backend->dumpCIA2Regs();
        }
        else if (subcmd == "ports")
        {
            std::cout << backend->dumpCIA2Ports();
        }
        else if (subcmd == "timers")
        {
            std::cout << backend->dumpCIA2Timers();
        }
        else if (subcmd == "tod")
        {
            std::cout << backend->dumpCIA2TOD();
        }
        else if (subcmd == "icr")
        {
            std::cout << backend->dumpCIA2ICR();
        }
        else if (subcmd == "serial")
        {
            std::cout << backend->dumpCIA2Serial();
        }
        else if (subcmd == "vic")
        {
            std::cout << backend->dumpCIA2VICBanks();
        }
        else if (subcmd == "iec")
        {
            std::cout << backend->dumpCIA2IECSnapshot();
            std::cout << backend->dumpCIA2IEC();
        }
        else
        {
            std::cout << "Unknown CIA2 subcommand: " << subcmd << "\n";
            std::cout << "Try: cia help\n";
        }
    }
    else
    {
        std::cout << "Error: CIA must be 1 or 2\n";
        std::cout << "Try: cia help\n";
    }
}
