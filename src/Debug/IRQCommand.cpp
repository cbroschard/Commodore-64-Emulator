// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Debug/IRQCommand.h"
#include "Debug/MLMonitor.h"
#include "Debug/MLMonitorBackend.h"

IRQCommand::IRQCommand() = default;

IRQCommand::~IRQCommand() = default;

int IRQCommand::order() const
{
    return 90;
}

std::string IRQCommand::category() const
{
    return "Hardware/Interrupts";
}

std::string IRQCommand::name() const
{
    return "irq";
}

std::string IRQCommand::shortHelp() const
{
    return "irq       - Manipulate or inspect interrupt sources/line behavior";
}

std::string IRQCommand::help() const
{
    return
          "irq status     - Display current status of IRQ/NMI sources.\n"
          "irq on         - Force a monitor-generated IRQ source active for CPU testing.\n"
          "irq off        - Disable interrupt sources and clear pending interrupts.\n"
          "irq clear      - Acknowledge/clear pending interrupts without changing masks.\n"
          "irq restore    - Restore IRQ enables from snapshot and clear monitor-forced IRQ.\n"
          "irq vic <m>    - Set VIC $D01A to mask m. Bits: 0=raster,1=spr-bg,2=spr-spr,3=lightpen.\n"
          "irq cia1 <m>   - Enable CIA1 IER bits m (0..31). Monitor remembers what it sets.\n"
          "irq cia2 <m>   - Enable CIA2 IER bits m (0..31). CIA2 drives NMI.\n"
          "irq sei        - Set CPU I flag (disable maskable IRQs).\n"
          "irq cli        - Clear CPU I flag (enable maskable IRQs).\n";
}

void IRQCommand::execute(MLMonitor& mon, const std::vector<std::string>& args)
{
    auto printHex2 = [](uint8_t v)
    {
        std::ios::fmtflags f(std::cout.flags());
        std::cout << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << int(v);
        std::cout.flags(f);
    };

    auto showStatus = [&]()
    {
        std::cout << "VIC : IER=$";  printHex2(mon.mlmonitorbackend()->vicIER());
        std::cout << " IFR=$";       printHex2(mon.mlmonitorbackend()->vicIFR());
        std::cout << " IRQ=" << (mon.mlmonitorbackend()->vicIRQ() ? "asserted" : "clear") << "\n";

        std::cout << "CIA1: IER=$";  printHex2(mon.mlmonitorbackend()->cia1IER());
        std::cout << " IFR=$";       printHex2(mon.mlmonitorbackend()->cia1IFR());
        std::cout << " IRQ=" << (mon.mlmonitorbackend()->cia1IRQ() ? "asserted" : "clear") << "\n";

        std::cout << "CIA2: IER=$";  printHex2(mon.mlmonitorbackend()->cia2IER());
        std::cout << " IFR=$";       printHex2(mon.mlmonitorbackend()->cia2IFR());
        std::cout << " NMI=" << (mon.mlmonitorbackend()->cia2NMI() ? "asserted" : "clear") << "\n";

        std::cout << "IRQ : line="
          << (mon.mlmonitorbackend()->irqLineActive() ? "active" : "inactive")
          << "\n";

        uint8_t sr = mon.mlmonitorbackend()->cpuGetSR();
        std::cout << "CPU : SR=$";  printHex2(sr);
        std::cout << " I=" << ((sr & CPU::I) ? "1" : "0") << " ("
                  << ((sr & CPU::I) ? "disabled" : "enabled") << ")\n";
    };

    if (args.size() == 1) { showStatus(); std::cout << shortHelp() << "\n"; return; }

    const std::string& sub = args[1];

    if (isHelp(sub))
    {
        std::cout << help();
        return;
    }
    else if (sub == "on")
    {
        mon.mlmonitorbackend()->irqForceOn();
        std::cout << "Monitor IRQ source forced active.\n";
        showStatus();
        return;
    }
    else if (sub == "off")
    {
        mon.mlmonitorbackend()->irqDisableAll();
        std::cout << "Interrupt sources disabled and pending cleared.\n";
        showStatus();
        return;
    }
    else if (sub == "clear")
    {
        mon.mlmonitorbackend()->irqClearAll();
        std::cout << "Pending interrupts cleared.\n";
        showStatus();
        return;
    }
    else if (sub == "restore")
    {
        mon.mlmonitorbackend()->irqRestore();
        std::cout << "IRQ masks restored from snapshot.\n";
        showStatus();
        return;
    }
    else if (sub == "vic" || sub == "cia1" || sub == "cia2")
    {
        if (args.size() < 3)
        {
            std::cout << "Missing <mask>. Usage: " << shortHelp() << "\n";
            return;
        }

        uint8_t m = 0;
        try
        {
            m = static_cast<uint8_t>(parseAddress(args[2]) & 0xFF);
        }
        catch (...)
        {
            std::cout << "Bad mask: " << args[2] << "\n";
            return;
        }

        if (sub == "vic")  { mon.mlmonitorbackend()->setVicIER (m & 0x0F); std::cout << "VIC  IER <= $";  printHex2(m & 0x0F); std::cout << "\n"; }
        if (sub == "cia1") { mon.mlmonitorbackend()->setCIA1IER(m & 0x1F); std::cout << "CIA1 IER <= $";  printHex2(m & 0x1F); std::cout << "\n"; }
        if (sub == "cia2") { mon.mlmonitorbackend()->setCIA2IER(m & 0x1F); std::cout << "CIA2 IER <= $";  printHex2(m & 0x1F); std::cout << "\n"; }

        showStatus();
        return;
    }
    else if (sub == "sei")
    {
        mon.mlmonitorbackend()->cpuSEI();
        std::cout << "CPU: SEI (I=1). Maskable IRQs disabled.\n";
        showStatus();
        return;
    }
    else if (sub == "cli")
    {
        mon.mlmonitorbackend()->cpuCLI();
        std::cout << "CPU: CLI (I=0). Maskable IRQs enabled.\n";
        showStatus();
        return;
    }
    else if (sub == "status")
    {
        showStatus();
        return;
    }
    else
    {
        std::cout << help() << "\n";
        return;
    }
}
