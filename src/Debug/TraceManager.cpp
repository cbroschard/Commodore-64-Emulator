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
    cart(nullptr),
    cia1object(nullptr),
    cia2object(nullptr),
    processor(nullptr),
    mem(nullptr),
    pla(nullptr),
    sidchip(nullptr),
    vicII(nullptr),
    tracing(false)
{
    cats = static_cast<uint32_t>(0u);
}

TraceManager::~TraceManager()
{
    if (file.is_open()) file.close();
}

const char* TraceManager::sidRegNames[32] =
{
    "Voice1_Frequency_Lo", "Voice1_Frequency_Hi", "Voice1_Pulsewidth_Lo", "Voice1_Pulsewidth_Hi", "Voice1_Control",
    "Voice1_AttackDecay", "Voice1_SustainRelease", "Voice2_Frequency_Lo", "Voice2_Frequency_Hi", "Voice2_Pulsewidth_Lo",
    "Voice2_Pulsewidth_Hi", "Voice2_Control", "Voice2_AttackDecay", "Voice2_SustainRelease", "Voice3_Frequency_Lo",
    "Voice3_Frequency_Hi", "Voice3_PulseWidth_Lo", "Voice3_Pulsewidth_Hi", "Voice3_Control", "Voice3_AttackDecay",
    "Voice3_SustainRelease", "Filter_Cutoff_Lo", "Filter_Cutoff_Hi", "FILTER_RESON_ROUTE", "Volume_FilterMode", "POT_X",
    "POT_Y", "OSC3_RANDOM", "ENV3_OUTPUT", "UNUSED_1C", "UNUSED_1D", "UNUSED_1E"
};

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

std::string TraceManager::listCategoryStatus()
{
    std::ostringstream out;
    out << "Trace " << (isEnabled() ? "ON" : "OFF")
        << "  mask=0x" << std::hex << std::uppercase << std::setw(8)
        << std::setfill('0') << categories() << std::dec << "\n";

    bool first = true;
    for (auto [cat, name] : catNames)
    {
        if (!first) out << ", ";
        first = false;
        out << name << "=" << (catOn(cat) ? "on" : "off");
    }
    return out.str();
}

bool TraceManager::memRangeContains(uint16_t address) const
{
    for (auto& range : memRanges)
    {
        if (range.contains(address)) return true;
    }

    return false;
}

std::string TraceManager::listMemRange() const
{
    std::stringstream out;
    bool first = true;
    for (const auto& range : memRanges)
    {
        if (!first) out << ", ";
        out << "Lo=$" << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << range.lo
            << " Hi=$" << std::setw(4) << range.hi;
        first = false;
    }
    return out.str();
}

void TraceManager::recordCartBank(const char* mapper, int bank, uint16_t lo, uint16_t hi, Stamp stamp)
{
    if (!tracing || !catOn(TraceCat::CART)) return;

    std::stringstream out;

    out << makeStamp(stamp) << "Mapper: " << mapper << " Bank: " << bank << " CART_LO: " << std::hex << std::setw(4) << lo << " CART_HI: " << hi;
    buffer.push_back(out.str());
    if (file.is_open()) file << buffer.back() << "\n";
}

void TraceManager::recordCPUTrace(uint16_t pcExec, uint8_t opcode, Stamp stamp)
{
    if (!tracing || !processor || !catOn(TraceCat::CPU)) return;

    std::stringstream out;

    // Do heading first
    out << makeStamp(stamp);

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

void TraceManager::recordMemRead(uint16_t address, uint8_t value, uint16_t pc, Stamp stamp)
{
    if (!tracing || !catOn(TraceCat::MEM)) return;

    std::stringstream out;

    out << makeStamp(stamp) << "R: Address=$" << std::hex << std::uppercase << std::setfill('0')
        << std::setw(4) << address
        << " Value=$" << std::setw(2) << int(value)
        << " PC=$" << std::setw(4) << pc;

    buffer.push_back(out.str());
    if (file.is_open()) file << buffer.back() << "\n";
}

void TraceManager::recordMemWrite(uint16_t address, uint8_t value, uint16_t pc, Stamp stamp)
{
    if (!tracing || !catOn(TraceCat::MEM)) return;

    std::stringstream out;

    out << makeStamp(stamp) << "W: Address=$" << std::hex << std::uppercase << std::setfill('0')
        << std::setw(4) << address
        << " Value=$" << std::setw(2) << int(value)
        << " PC=$" << std::setw(4) << pc;

    buffer.push_back(out.str());
    if (file.is_open()) file << buffer.back() << "\n";
}

void TraceManager::recordPlaMode(uint8_t mode, bool game, bool exrom, bool charen, bool hiram, bool loram, Stamp stamp)
{
    if (!tracing || !catOn(TraceCat::PLA)) return;

    std::stringstream out;

    out << makeStamp(stamp) << "PLA Mode: " << int(mode) << " Game Line: " << (game ? "1 (inactive)" : "0 (asserted)") <<
        " exRom: " << (exrom ? "1 (inactive" : "0 (asserted)") << " HIRAM: " << hiram << " LORAM: " << loram;

    buffer.push_back(out.str());
    if (file.is_open()) file << buffer.back() << "\n";
}

void TraceManager::recordSidWrite(uint16_t reg, uint8_t val, Stamp stamp)
{
    if (!tracing || !catOn(TraceCat::SID)) return;

    std::stringstream out;

    if (reg < 32)
    {
        out << makeStamp(stamp) << "[SID] " << sidRegNames[reg] << " = $" << std::hex << +val;
    }

    buffer.push_back(out.str());
    if (file.is_open()) file << buffer.back() << "\n";;
}

void TraceManager::recordCustomEvent(const std::string& text)
{
    if (tracing)
    {
        buffer.push_back(text);
        if (file.is_open()) file << text << "\n";
    }
}

std::string TraceManager::makeStamp(const Stamp& stamp) const
{
    std::stringstream out;

    out << "[" << stamp.cycles << " " <<  std::hex << std::uppercase << std::setw(3) << std::setfill('0')
        << stamp.rasterLine << "." << std::dec << std::setw(3) << std::setfill('0') << stamp.rasterDot <<  "]";

    return out.str();
}

uint32_t TraceManager::catToMask(TraceCat cat)
{
    switch (cat)
    {
        case TraceCat::CPU:  return 1u<<0;
        case TraceCat::VIC:  return 1u<<1;
        case TraceCat::CIA1: return 1u<<2;
        case TraceCat::CIA2: return 1u<<3;
        case TraceCat::PLA:  return 1u<<4;
        case TraceCat::SID:  return 1u<<5;
        case TraceCat::CART: return 1u<<6;
        case TraceCat::MEM:  return 1u<<7;
    }
    // Default
    return 1u<<0;
}
