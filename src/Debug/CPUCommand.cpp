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
    return "cpu       - CPU commands (regs, irq, cycles, stack, jam)";
}

std::string CPUCommand::help() const
{
    return R"(CPU commands
 Usage:
  cpu addr                      - Show last indirect addressing details
  cpu branch                    - Show last branch timing details
  cpu busarb [status|on|off]    - Show or change bus arbitration
  cpu cycles                    - Show CPU cycle counters and current timing state
  cpu intr                      - Show the last BRK/IRQ/NMI entry details.
  cpu irq                       - Show IRQ/NMI timing state
  cpu jam                       - Show or set JAM/KIL opcode behavior
  cpu jmp                       - Show last JMP target details
  cpu jsr                       - Show last JSR stack details
  cpu last                      - Show last executed opcode and timing position
  cpu micro [status|on|off]     - Show or change experimental CPU micro-op execution
  cpu pc <addr>                 - Set CPU program counter
  cpu pha                       - Show last PHA stack push details
  cpu php                       - Show last PHP status push details
  cpu pla                       - Show last PLA stack pull details
  cpu plp                       - Show last PLP status restore details
  cpu regs                      - Show CPU registers
  cpu rti                       - Show last RTI return details
  cpu rts                       - Show last RTS return details
  cpu stack [count]             - Show stack contents
)";
}

std::string CPUCommand::jamUsage() const
{
    return R"(cpu jam [mode]

Usage:
    cpu jam           Show the current JAM handling mode
    cpu jam freeze    Freeze PC when a JAM/KIL is encountered
    cpu jam halt      Halt CPU execution on JAM/KIL
    cpu jam nop       Treat JAM/KIL as a 2-byte NOP

Description:
    Controls how the emulator reacts when encountering
    undocumented KIL/JAM opcodes. By default, most demos
    expect Freeze or NOP handling instead of a hard halt.
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
    else if (sub == "addr" || sub == "address")
    {
        std::cout << mon.mlmonitorbackend()->cpuAddressStatus();
        return;
    }
    else if (sub == "branch" || sub == "br")
    {
        std::cout << mon.mlmonitorbackend()->cpuBranchStatus();
        return;
    }
    else if (sub == "busarb")
    {
        if (args.size() == 2 || args[2] == "status" )
        {
            std::cout << "CPU BUS Arbitration " << (mon.mlmonitorbackend()->cpuIsBusArbEnabled() ? "enabled\n" : "disabled\n");
            return;
        }
        else if (args[2] == "on")
        {
            mon.mlmonitorbackend()->cpuSetBusArbEnabled(true);
            std::cout << "CPU BUS Arbitration enabled\n";
            return;
        }
        else if (args[2] == "off")
        {
            mon.mlmonitorbackend()->cpuSetBusArbEnabled(false);
            std::cout << "CPU BUS Arbitration disabled\n";
            return;
        }
        else
        {
            std::cout << help();
            return;
        }
    }
    else if (sub == "cycles")
    {
        std::cout << mon.mlmonitorbackend()->cpuCycleStatus();
        return;
    }
    else if (sub == "intr" || sub == "interrupt")
    {
        std::cout << mon.mlmonitorbackend()->cpuInterruptStatus();
        return;
    }
    else if (sub == "irq")
    {
        std::cout << mon.mlmonitorbackend()->cpuIrqStatus();
        return;
    }
    else if (sub == "micro")
    {
        if (args.size() == 2 || args[2] == "status" )
        {
            std::cout << mon.mlmonitorbackend()->cpuMicroOpStatus();
            return;
        }
        else if (args[2] == "on")
        {
            mon.mlmonitorbackend()->cpuSetMicroOp(true);
            std::cout << "Micro Op enabled\n";
            return;
        }
        else if (args[2] == "off")
        {
            mon.mlmonitorbackend()->cpuSetMicroOp(false);
            std::cout << "Micro Op disabled\n";
            return;
        }
        else
        {
            std::cout << help();
            return;
        }
    }
    else if (sub == "jam")
    {
        if (args.size() >= 3)
        {
            if (isHelp(args[2]))
            {
                std::cout << jamUsage();
                return;
            }
            else if (args[2] == "freeze")
            {
                mon.mlmonitorbackend()->setJamMode("freeze");
                std::cout << "Updated Jam mode to FreezePC\n";
                return;
            }
            else if (args[2] == "halt")
            {
                mon.mlmonitorbackend()->setJamMode("halt");
                std::cout << "Updated Jam mode to Halt.\n";
                return;
            }
            else if (args[2] == "nop")
            {
                mon.mlmonitorbackend()->setJamMode("nop");
                std::cout << "Updated Jam mode to NopCompat\n";
                return;
            }
            else
            {
                std::cout << "Invalid JAM mode: " << args[2] << "\n";
                std::cout << jamUsage();
                return;
            }
        }
        else
        {
            // Show current mode
            std::cout << "The current Jam mode is: " << mon.mlmonitorbackend()->getJamMode() << "\n";
            return;
        }
    }
    else if (sub == "jmp")
    {
        std::cout << mon.mlmonitorbackend()->cpuJMPStatus();
        return;
    }
    else if (sub == "jsr")
    {
        std::cout << mon.mlmonitorbackend()->cpuJSRStatus();
        return;
    }
    else if (sub == "last")
    {
        std:: cout << mon.mlmonitorbackend()->cpuLastStatus();
        return;
    }
    else if (sub == "pc")
    {
        if (args.size() < 3)
        {
            std::cout << "Usage: cpu pc <addr>\n";
            return;
        }

        try
        {
            const uint16_t addr = parseAddress(args[2]);
            mon.mlmonitorbackend()->setPC(addr);
            std::cout << "PC set to $" << std::uppercase << std::hex
                      << std::setw(4) << std::setfill('0') << int(addr) << "\n";
        }
        catch (...)
        {
            std::cout << "Bad address: " << args[2] << "\n";
        }

        return;
    }
    else if (sub == "pha")
    {
        std::cout << mon.mlmonitorbackend()->cpuPHAStatus();
        return;
    }
    else if (sub == "php")
    {
        std::cout << mon.mlmonitorbackend()->cpuPHPStatus();
        return;
    }
    else if (sub == "pla")
    {
        std::cout << mon.mlmonitorbackend()->cpuPLAStatus();
        return;
    }
    else if (sub == "plp")
    {
        std::cout << mon.mlmonitorbackend()->cpuPLPStatus();
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
    else if (sub == "rti")
    {
        std::cout << mon.mlmonitorbackend()->cpuRTIStatus();
        return;
    }
    else if (sub == "rts")
    {
        std::cout << mon.mlmonitorbackend()->cpuRTSStatus();
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
