// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Computer.h"
#include "Debug/IRQCommand.h"
#include "Debug/MLMonitor.h"

IRQCommand::IRQCommand() = default;

IRQCommand::~IRQCommand() = default;

int IRQCommand::order() const
{
    return 5;
}

std::string IRQCommand::category() const
{
    return "CPU/Execution";
}

std::string IRQCommand::name() const
{
    return "irq";
}

std::string IRQCommand::shortHelp() const
{
    return "irq [off|on|clear|vic <mask>|cia1 <mask>|cia2 <mask>]";
}

std::string IRQCommand::help() const
{
    return
          "irq off        - Disable all IRQ sources (VIC + CIA1 + CIA2) and clear any pending.\n"
          "irq on         - Restore IRQ enables from snapshot (taken when 'irq off' ran).\n"
          "irq clear      - Acknowledge/clear any pending interrupts without changing masks.\n"
          "irq vic <m>    - Set VIC $D01A to mask m (hex or dec). Bits: 0=raster,1=spr-spr,2=spr-bg,3=lightpen.\n"
          "irq cia1 <m>   - Enable CIA1 IER bits m (0..31). (Write-only on HW; monitor remembers what it sets.)\n"
          "irq cia2 <m>   - Enable CIA2 IER bits m (0..31).\n";
}

void IRQCommand::execute(MLMonitor& mon, const std::vector<std::string>& args)
{

}
