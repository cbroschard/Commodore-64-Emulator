#include "Computer.h"
#include "Debug/CIACommand.h"
#include "Debug/MLMonitor.h"

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
    return "cia <1|2> <subcmd> - Inspect CIA1 or CIA2 (regs, timers, tod, icr, mode/iec)";
}

std::string CIACommand::help() const
{
    return R"(cia - Inspect CIA1 or CIA2

Usage:
    cia <1|2> <subcommand>

Subcommands (common to CIA1 and CIA2):
    ports     - Show all ports (A & B) along with Data Direction registers
    regs      - Show all registers with decoded bit fields
    timers    - Show Timer A and Timer B state (latch, counter, mode)
    tod       - Show time-of-day clock (HH:MM:SS.tenth)
    icr       - Show interrupt control and pending sources

CIA1-only subcommands:
    mode      - Show keyboard/joystick input mode state

CIA2-only subcommands:
    iec       - Show IEC bus state, VIC bank select bits, and NMI source

Examples:
    cia 1 regs       Show CIA1 registers
    cia 2 timers     Display CIA2 Timer A/B values
    cia 1 mode       Show CIA1 keyboard/joystick mode
    cia 2 iec        Show CIA2 IEC serial bus lines
)";
}

void CIACommand::execute(MLMonitor& mon, const std::vector<std::string>& args)
{
    if (args.size() < 2)
    {
        std::cout << "Usage: cia <1|2> <subcommand>\n";
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
        return;
    }

    const std::string& subcmd = args[2];

    if (chipNum == 1)
    {
        if (subcmd == "regs")      std::cout << mon.computer()->dumpCIA1Regs();
        else if (subcmd == "ports")  std::cout << mon.computer()->dumpCIA1Ports();
        else if (subcmd == "timers") std::cout << mon.computer()->dumpCIA1Timers();
        else if (subcmd == "tod")    std::cout << mon.computer()->dumpCIA1TOD();
        else if (subcmd == "icr")    std::cout << mon.computer()->dumpCIA1ICR();
        else if (subcmd == "serial") std::cout << mon.computer()->dumpCIA1Serial();
        else if (subcmd == "mode")   std::cout << mon.computer()->dumpCIA1Mode();
        else if (subcmd == "help")   std::cout << help();
        else std::cout << help(); // default show help
    }
    else if (chipNum == 2)
    {
        if (subcmd == "regs")       std::cout << mon.computer()->dumpCIA2Regs();
        else if (subcmd == "ports")   std::cout << mon.computer()->dumpCIA2Ports();
        else if (subcmd == "timers")  std::cout << mon.computer()->dumpCIA2Timers();
        else if (subcmd == "tod")     std::cout << mon.computer()->dumpCIA2TOD();
        else if (subcmd == "icr")     std::cout << mon.computer()->dumpCIA2ICR();
        else if (subcmd == "serial")  std::cout << mon.computer()->dumpCIA2Serial();
        else if (subcmd == "vic")     std::cout << mon.computer()->dumpCIA2VICBanks();
        else if (subcmd == "iec")     std::cout << mon.computer()->dumpCIA2IEC();
        else if (subcmd == "help")    std::cout << help();
        else std::cout << help(); // default show help
    }
    else
    {
        std::cout << "Error: CIA must be 1 or 2\n";
    }
}
