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
    const auto st = mon.mlmonitorbackend()->getCPUState();

    auto hex2 = [](uint32_t v){
        std::ostringstream s;
        s << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << (v & 0xFF);
        return s.str();
    };
    auto hex4 = [](uint32_t v){
        std::ostringstream s;
        s << std::uppercase << std::hex << std::setw(4) << std::setfill('0') << (v & 0xFFFF);
        return s.str();
    };
    auto flagsBits = [&](uint8_t p){
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
}
