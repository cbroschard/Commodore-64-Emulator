// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Computer.h"
#include "Debug/MLMonitor.h"
#include "Debug/StepCommand.h"

StepCommand::StepCommand() = default;

StepCommand::~StepCommand() = default;

std::string StepCommand::name() const
{
    return "t";
}

std::string StepCommand::category() const
{
    return "CPU/Execution";
}

std::string StepCommand::shortHelp() const
{
    return
        "t         - Step one CPU instruction";
}

std::string StepCommand::help() const
{
    return
        "t    Execute exactly one CPU instruction and then return to the monitor.\n"
        "     After stepping, registers are shown automatically.\n"
        "Examples:\n"
        "    t        Step one CPU instruction";
}

void StepCommand::execute(MLMonitor& mon, const std::vector<std::string>& args)
{
    if (args.size() > 1 && isHelp(args[1]))
    {
        std::cout << help() << std::endl;
        return;
    }

    // Determine current CPU PC
    uint16_t pc = mon.computer()->getPC();
    Memory* mem = mon.computer()->getMem();

    // Check for raster check loop, if so, fast forward to it
    uint8_t targetRaster;
    if (mon.isRasterWaitLoop(pc, targetRaster))
    {
        if (mon.computer()->getCurrentRaster() != targetRaster)
        {
            std::cout << "[Monitor] Raster wait detected at $"
                  << std::hex << pc
                  << ", fast-forwarding to line $"
                  << int(targetRaster) << std::endl;
            mon.computer()->vicFFRaster(targetRaster);
        }
    }

    // Output disassembly at PC
    std::string disASM = Disassembler::disassembleAt(pc, *mem);
    std::cout << disASM << std::endl;

    // Execute OPCODE
    mon.computer()->cpuStep();

    // Dump CPU registers
    auto st = mon.computer()->getCPUState();
    std::cout << "PC=$" << std::setw(4) << std::setfill('0') << std::hex << std::uppercase << st.PC
        << "  A=$" << std::setw(2) << int(st.A)
        << "  X=$" << std::setw(2) << int(st.X)
        << "  Y=$" << std::setw(2) << int(st.Y)
        << "  SP=$" << std::setw(2) << int(st.SP)
        << "  P=$" << std::setw(2) << int(st.SR)
        << "  (NV-BDIZC)\n";
}
