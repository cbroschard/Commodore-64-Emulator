// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Computer.h"
#include "Debug/MLMonitor.h"
#include "Debug/SIDCommand.h"

SIDCommand::SIDCommand() = default;

SIDCommand::~SIDCommand() = default;

int SIDCommand::order() const
{
    return 1;
}

std::string SIDCommand::name() const
{
    return "sid";
}

std::string SIDCommand::category() const
{
    return "Chip/SID";
}

std::string SIDCommand::shortHelp() const
{
    return "sid       - Show SID registers, voices, or filter state";
}

std::string SIDCommand::help() const
{
    return R"(sid - SID chip inspection commands

 Usage:
    sid all         Dump all SID registers ($D400-$D418)
    sid voice1      Dump registers/state for Voice 1
    sid voice2      Dump registers/state for Voice 2
    sid voice3      Dump registers/state for Voice 3
    sid voices      Dump summary of all 3 voices
    sid filter      Dump filter and volume registers
    sid help        Show this help page

 Description:
    The 'sid' command lets you inspect the current state of the SID sound chip.
    You can view all registers, focus on a single voice, or check the filter
    configuration. This is useful for debugging music playback, testing ADSR
    envelope behavior, and confirming filter routing.

 Examples:
    sid all         Show all register values
    sid voice1      Inspect ADSR/waveform/regs for Voice 1
    sid voices      Summarize the 3 voices (ADSR + envelope levels)
    sid filter      Inspect filter cutoff/resonance and volume control
)";
}

void SIDCommand::execute(MLMonitor& mon, const std::vector<std::string>& args)
{
    if (args.size() < 2 || (args.size() == 2 && isHelp(args[1])))
    {
        std::cout << help();
        return;
    }

    const std::string& subcmd = args[1];

    if (subcmd == "all") std::cout << mon.mlmonitorbackend()->dumpSIDRegs();
    else if (subcmd == "voice1")  std::cout << mon.mlmonitorbackend()->dumpSIDVoice1();
    else if (subcmd == "voice2")  std::cout << mon.mlmonitorbackend()->dumpSIDVoice2();
    else if (subcmd == "voice3")  std::cout << mon.mlmonitorbackend()->dumpSIDVoice3();
    else if (subcmd == "voices")  std::cout << mon.mlmonitorbackend()->dumpSIDVoices();
    else if (subcmd == "filter")  std::cout << mon.mlmonitorbackend()->dumpSIDFilter();
    else if (subcmd == "help")    std::cout << help();
    else std::cout << help(); // default show help
}
