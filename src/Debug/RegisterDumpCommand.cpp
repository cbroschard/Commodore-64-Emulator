// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Computer.h"
#include "Debug/MLMonitor.h"
#include "Debug/RegisterDumpCommand.h"

RegisterDumpCommand::RegisterDumpCommand() = default;

RegisterDumpCommand::~RegisterDumpCommand() = default;

std::string RegisterDumpCommand::name() const
{
    return "r";
}

std::string RegisterDumpCommand::category() const
{
    return "CPU/Execution";
}

std::string RegisterDumpCommand::shortHelp() const
{
    return "r         - Show CPU registers";
}

std::string RegisterDumpCommand::help() const
{
     return "r                  - Show CPU registers (PC, A, X, Y, SP, status flags)";
}

void RegisterDumpCommand::execute(MLMonitor& mon, const std::vector<std::string>&)
{
    auto st = mon.computer()->getCPUState();
    std::cout << "PC=$" << std::setw(4) << std::setfill('0') << std::hex << std::uppercase << st.PC
        << "  A=$" << std::setw(2) << int(st.A)
        << "  X=$" << std::setw(2) << int(st.X)
        << "  Y=$" << std::setw(2) << int(st.Y)
        << "  SP=$" << std::setw(2) << int(st.SP)
        << "  P=$" << std::setw(2) << int(st.SR)
        << "  (NV-BDIZC)\n";
}
