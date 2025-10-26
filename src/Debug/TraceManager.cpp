// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "CPU.h"
#include "Debug/TraceManager.h"

TraceManager::TraceManager() :
    cia1object(nullptr),
    cia2object(nullptr),
    processor(nullptr),
    mem(nullptr),
    sidchip(nullptr),
    vicII(nullptr),
    tracing(false)
{

}

TraceManager::~TraceManager()
{
    if (file.is_open()) file.close();
}

void TraceManager::enable(bool on)
{
    tracing = on;
    if (!on)
    {
        dumpBuffer();
    }
}

void TraceManager::setFileOutput(const std::string& path)
{
    if (file.is_open()) file.close();
    file.open(path, std::ios::out | std::ios::trunc);
}

void TraceManager::dumpBuffer()
{
    for (auto& line : buffer) std::cout << line << "\n";
    buffer.clear();
}

void TraceManager::clearBuffer()
{
    buffer.clear();
}

void TraceManager::recordCPUTrace(uint16_t pcExec, uint8_t opcode)
{
    if (!tracing || !processor) return;

    std::stringstream out;
    auto st = processor->getState();
    out << std::hex << std::uppercase << std::setfill('0')
        << "PC=$"  << std::setw(4) << pcExec
        << " OPC=$"<< std::setw(2) << int(opcode)
        << "  A=$" << std::setw(2) << int(st.A)
        << "  X=$" << std::setw(2) << int(st.X)
        << "  Y=$" << std::setw(2) << int(st.Y)
        << "  SP=$"<< std::setw(2) << int(st.SP)
        << "  P="  << std::bitset<8>(st.SR);

    buffer.push_back(out.str());
    if (file.is_open()) file << buffer.back() << "\n";
}

void TraceManager::recordCustomEvent(const std::string& text)
{
    if (tracing)
    {
        buffer.push_back(text);
        if (file.is_open()) file << text << "\n";
    }
}
