// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Debug/MLMonitor.h"
#include "Debug/MLMonitorBackend.h"
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
    return "sid       - Show SID registers, voices, filter, cutoff, or audio state";
}

std::string SIDCommand::help() const
{
    return R"(sid - SID chip inspection commands

Usage:
    sid all             Dump all SID registers ($D400-$D418)
    sid voice1          Dump registers/state for Voice 1
    sid voice2          Dump registers/state for Voice 2
    sid voice3          Dump registers/state for Voice 3
    sid voices          Dump summary of all 3 voices
    sid filter          Dump filter and volume registers
    sid cutoff          Show model-specific cutoff mapping table
    sid audio           Dump SID audio queue / underrun health
    sid audio reset     Reset SID audio underrun/stat counters
    sid help            Show this help page

Description:
    The 'sid' command lets you inspect the current state of the SID sound chip.
    You can view all registers, inspect individual voices, check filter
    configuration, preview cutoff mapping, or inspect SDL/SID audio-buffer health.

Examples:
    sid all             Show all register values
    sid voice1          Inspect ADSR/waveform/registers for Voice 1
    sid voices          Summarize the 3 voices
    sid filter          Inspect filter cutoff/resonance and volume control
    sid cutoff          Preview model-specific cutoff mapping table
    sid audio           Dump SID audio queue / underrun health
    sid audio reset     Reset SID audio underrun/stat counters
)";
}

void SIDCommand::execute(MLMonitor& mon, const std::vector<std::string>& args)
{
    if (args.size() < 2 || isHelp(args[1]))
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

    const std::string& subcmd = args[1];

    if (subcmd == "all")
    {
        std::cout << backend->dumpSIDRegs();
        return;
    }

    if (subcmd == "voice1")
    {
        std::cout << backend->dumpSIDVoice1();
        return;
    }

    if (subcmd == "voice2")
    {
        std::cout << backend->dumpSIDVoice2();
        return;
    }

    if (subcmd == "voice3")
    {
        std::cout << backend->dumpSIDVoice3();
        return;
    }

    if (subcmd == "voices")
    {
        std::cout << backend->dumpSIDVoices();
        return;
    }

    if (subcmd == "filter")
    {
        std::cout << backend->dumpSIDFilter();
        return;
    }

    if (subcmd == "cutoff")
    {
        std::cout << backend->dumpSIDCutoffTable();
        return;
    }

    if (subcmd == "audio")
    {
        if (args.size() >= 3)
        {
            if (args[2] == "reset")
            {
                backend->resetSIDAudioStats();
                std::cout << "SID audio stats reset.\n";
                return;
            }

            if (isHelp(args[2]))
            {
                std::cout << "Usage: sid audio [reset]\n";
                return;
            }

            std::cout << "Unknown SID audio subcommand: " << args[2] << "\n";
            std::cout << "Usage: sid audio [reset]\n";
            return;
        }

        std::cout << backend->dumpSIDAudio();
        return;
    }

    std::cout << "Unknown SID subcommand: " << subcmd << "\n";
    std::cout << "Try: sid help\n";
}
