// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Computer.h"
#include "Debug/IECCommand.h"
#include "Debug/MLMonitor.h"

IECCommand::IECCommand() = default;

IECCommand::~IECCommand() = default;

int IECCommand::order() const
{
    return 25;
}

std::string IECCommand::name() const
{
    return "iec";
}

std::string IECCommand::category() const
{
    return "Drives and IEC Bus";
}

std::string IECCommand::shortHelp() const
{
    return "iec           - IEC bus lines & protocol";
}

std::string IECCommand::help() const
{
    return R"(iec  - Show IEC serial bus status

Usage:
  iec [subcmd]

Subcommands:
  (none), all   Show full IEC status:
    - Bus line levels (ATN/CLK/DATA/SRQ)
    - C64/peripheral drivers (who pulls low)
    - Protocol state (IDLE/TALK/LISTEN/...)
    - Current talker & listeners
    - Registered devices

  bus           Show bus line levels only:
                ATN / CLK / DATA / SRQ as H (released/high)
                or L (pulled low)

  drivers       Show which side is pulling lines low:
                C64 vs peripherals, 0 = pulling low, 1 = released

  state         Show IEC protocol state plus current
                talker/listeners only

  devices       List attached IEC devices, e.g. #8 #9 #10

  device <n>    Show detailed info for device number <n> (if attached)
)";
}

void IECCommand::execute(MLMonitor& mon, const std::vector<std::string>& args)
{
    IECBUS* bus = mon.mlmonitorbackend()->getIECBus();
    if (!bus)
    {
        std::cout << "Bus not attached!\n";
        return;
    }

    auto bit = [](bool drivesLow) { return drivesLow ? '0' : '1'; }; // 0 = pulling low

    const std::string& sub = args[1];

    if (isHelp(sub))
    {
        std::cout << help();
        return;
    }

    // BUS
    if ((args.size() == 1 || sub == "all") || (sub == "bus"))
    {
        const IECBusLines& lines = bus->getBusLines();
        bool srq = bus->getSRQLine();

        auto hl  = [](bool v) { return v ? 'H' : 'L'; };          // H/L for lines

        // 1) Bus line levels
        std::cout << "IEC bus:\n";
        std::cout << "  Lines: ATN=" << hl(lines.atn)
                  << "  CLK="  << hl(lines.clk)
                  << "  DATA=" << hl(lines.data)
                  << "  SRQ="  << hl(srq) << "\n";
        std::cout << "         (H = released/high, L = pulled low)\n\n";
    }

    // Drivers
    if ((args.size() == 1 || sub == "all") || (sub == "drivers"))
    {
        std::cout << "Drivers (0 = pulling low, 1 = released):\n";
        std::cout << "  C64:         ATN=" << bit(bus->getC64DrivesAtnLow())
                  << "  CLK="  << bit(bus->getC64DrivesClkLow())
                  << "  DATA=" << bit(bus->getC64DrivesDataLow())
                  << "\n";
        std::cout << "  Peripherals: ATN=" << bit(bus->getPeripheralDrivesAtnLow())
                  << "  CLK="  << bit(bus->getPeripheralDrivesClkLow())
                  << "  DATA=" << bit(bus->getPeripheralDrivesDataLow())
                  << "\n\n";
    }

    // State
    if ((args.size() == 1 || sub == "all") || (sub == "state"))
    {
        const char* stateStr = "UNKNOWN";
        switch (bus->getState())
        {
            case IECBUS::State::IDLE:      stateStr = "IDLE";      break;
            case IECBUS::State::ATTENTION: stateStr = "ATTENTION"; break;
            case IECBUS::State::TALK:      stateStr = "TALK";      break;
            case IECBUS::State::LISTEN:    stateStr = "LISTEN";    break;
            case IECBUS::State::UNLISTEN:  stateStr = "UNLISTEN";  break;
            case IECBUS::State::UNTALK:    stateStr = "UNTALK";    break;
        }

        std::cout << "State:\n";
        std::cout << "  Mode: " << stateStr << "\n\n";

        // Talkers/Listeners
        Peripheral* talker = bus->getCurrentTalker();
        std::cout << "Talker / listeners:\n";
        if (talker)
        {
            std::cout << "  Current talker:    #"
                      << talker->getDeviceNumber() << "\n";
        }
        else
        {
            std::cout << "  Current talker:    (none)\n";
        }

        const auto& listeners = bus->getCurrentListeners();
        std::cout << "  Current listeners: ";
        if (listeners.empty())
        {
            std::cout << "(none)\n";
        }
        else
        {
            bool first = true;
            for (auto* dev : listeners)
            {
                if (!dev) continue;
                if (!first) std::cout << ' ';
                std::cout << "#" << dev->getDeviceNumber();
                first = false;
            }
            std::cout << "\n";
        }
        std::cout << "\n";
    }

    // Devices
    if ((args.size() == 1 || sub == "all") || (sub == "devices"))
    {
        const auto& devMap = bus->getDevices();
        std::cout << "Devices:\n";
        if (devMap.empty())
        {
            std::cout << "  (none)\n";
        }
        else
        {
            for (const auto& [num, dev] : devMap)
            {
                if (!dev) continue;
                std::cout << "  #" << num << "\n";
            }
        }
    }
}

