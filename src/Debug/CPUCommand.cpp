// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Debug/CPUCommand.h"
#include "Debug/MLMonitor.h"
#include "Debug/MLMonitorBackend.h"

CPUCommand::CPUCommand() = default;

CPUCommand::~CPUCommand() = default;

int CPUCommand::order() const
{
    return 6;
}

std::string CPUCommand::name() const
{
    return "cpu";
}

std::string CPUCommand::category() const
{
    return "CPU/Execution";
}

std::string CPUCommand::shortHelp() const
{
    return "cpu       - CPU commands (regs, irq, cycles, stack, jam, trace)";
}

std::string CPUCommand::help() const
{
    return R"(CPU commands
 Usage:
  cpu regs              - Show CPU registers
  cpu irq               - Show IRQ/NMI timing state
  cpu cycles            - Show CPU cycle counters and current timing state
  cpu stack [count]     - Show stack contents
  cpu jam               - Show or set JAM/KIL opcode behavior
)";
}

void CPUCommand::execute(MLMonitor& mon, const std::vector<std::string>& args)
{
    if (args.size() < 2)
    {
        std::cout << help() << std::endl;
        return;
    }

    const std::string& sub = args[1];

    if (isHelp(sub))
    {
        std::cout << "Usage:\n" << help() << std::endl;
        return;
    }
    else if (sub == "regs")
    {
        const auto st = mon.mlmonitorbackend()->getCPUState();

        auto hex2 = [](uint32_t v)
        {
            std::ostringstream s;
            s << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << (v & 0xFF);
            return s.str();
        };

        auto hex4 = [](uint32_t v)
        {
            std::ostringstream s;
            s << std::uppercase << std::hex << std::setw(4) << std::setfill('0') << (v & 0xFFFF);
            return s.str();
        };

        auto flagsBits = [&](uint8_t p)
        {
            std::string b;
            b += (p & 0x80) ? '1' : '0'; // N
            b += (p & 0x40) ? '1' : '0'; // V
            b += '-';                    // (unused)
            b += (p & 0x10) ? '1' : '0'; // B
            b += (p & 0x08) ? '1' : '0'; // D
            b += (p & 0x04) ? '1' : '0'; // I
            b += (p & 0x02) ? '1' : '0'; // Z
            b += (p & 0x01) ? '1' : '0'; // C
            return b;
        };

        std::ostringstream out;
        out << "PC=$" << hex4(st.PC)
             << "  A=$" << hex2(st.A)
             << "  X=$" << hex2(st.X)
             << "  Y=$" << hex2(st.Y)
             << "  SP=$" << hex2(st.SP)
             << "  P=$" << hex2(st.SR)
             << "  (NV-BDIZC=" << flagsBits(st.SR) << ")\n";

        std::cout << out.str();
        return;
    }
    else if (sub == "irq")
    {
        if (mon.mlmonitorbackend())
            std::cout << mon.mlmonitorbackend()->cpuIrqStatus();
        return;
    }
    else if (sub == "cycles")
    {
        if (mon.mlmonitorbackend())
            std::cout << mon.mlmonitorbackend()->cpuCycleStatus();
        return;
    }
    else if (sub == "stack")
    {
        int count = 16;

        if (args.size() >= 3)
        {
            try
            {
                count = std::stoi(args[2]);
            }
            catch (...)
            {
                std::cout << "Invalid count. Usage: cpu stack [count]\n";
                return;
            }
        }

        if (mon.mlmonitorbackend())
            std::cout << mon.mlmonitorbackend()->cpuStackStatus(count);
        return;
    }
    else
    {
        std::cout << "Unknown cpu subcommand: " << sub << std::endl;
        std::cout << help() << std::endl;
        return;
    }
}
