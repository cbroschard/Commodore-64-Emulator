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
    tracing(false),
    chipCats(0u),
    detailCats(0ull)
{

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

void TraceManager::enableAllCategories(bool enable)
{
    for (auto [cat, name] : catNames)
    {
        if (enable) enableCategory(cat);
        else disableCategory(cat);
    }
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

bool TraceManager::cpuDetailOn(TraceDetail d) const
{
    return isEnabled() && catOn(TraceCat::CPU) && detailEnabled(d);
}

bool TraceManager::vicDetailOn(TraceDetail d) const
{
    return isEnabled() && catOn(TraceCat::VIC) && detailEnabled(d);
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

std::string TraceManager::listDetailStatus() const
{
    std::ostringstream out;

    out << "Details mask=0x"
        << std::hex << std::uppercase << std::setw(16)
        << std::setfill('0') << detailCats << std::dec << "\n";

    out << "CPU: "
        << "exec="   << (detailEnabled(TraceDetail::CPU_EXEC)   ? "on" : "off") << ", "
        << "irq="    << (detailEnabled(TraceDetail::CPU_IRQ)    ? "on" : "off") << ", "
        << "nmi="    << (detailEnabled(TraceDetail::CPU_NMI)    ? "on" : "off") << ", "
        << "stack="  << (detailEnabled(TraceDetail::CPU_STACK)  ? "on" : "off") << ", "
        << "branch=" << (detailEnabled(TraceDetail::CPU_BRANCH) ? "on" : "off") << ", "
        << "flags="  << (detailEnabled(TraceDetail::CPU_FLAGS)  ? "on" : "off") << ", "
        << "ba="     << (detailEnabled(TraceDetail::CPU_BA)     ? "on" : "off") << ", "
        << "jam="    << (detailEnabled(TraceDetail::CPU_JAM)    ? "on" : "off") << "\n";

    out << "VIC: "
        << "raster="  << (detailEnabled(TraceDetail::VIC_RASTER)  ? "on" : "off") << ", "
        << "irq="     << (detailEnabled(TraceDetail::VIC_IRQ)     ? "on" : "off") << ", "
        << "reg="     << (detailEnabled(TraceDetail::VIC_REG)     ? "on" : "off") << ", "
        << "badline=" << (detailEnabled(TraceDetail::VIC_BADLINE) ? "on" : "off") << ", "
        << "sprite="  << (detailEnabled(TraceDetail::VIC_SPRITE)  ? "on" : "off") << ", "
        << "bus="     << (detailEnabled(TraceDetail::VIC_BUS)     ? "on" : "off") << ", "
        << "event="   << (detailEnabled(TraceDetail::VIC_EVENT)   ? "on" : "off");

     out << "CIA: "
        << "timer="   << (detailEnabled(TraceDetail::CIA_TIMER) ? "on" : "off") << ", "
        << "irq="     << (detailEnabled(TraceDetail::CIA_IRQ)   ? "on" : "off") << ", "
        << "cnt="     << (detailEnabled(TraceDetail::CIA_CNT)   ? "on" : "off") << ", "
        << "iec="     << (detailEnabled(TraceDetail::CIA_IEC)   ? "on" : "off");

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

bool TraceManager::ciaDetailOn(int cia, TraceDetail d) const
{
    if (!isEnabled() || !detailEnabled(d))
        return false;

    if (cia == 1) return catOn(TraceCat::CIA1);
    if (cia == 2) return catOn(TraceCat::CIA2);
    return false;
}

void TraceManager::enableCIADetails(bool enable)
{
    const TraceDetail ciaDetails[] =
    {
        TraceDetail::CIA_TIMER,
        TraceDetail::CIA_IRQ,
        TraceDetail::CIA_CNT,
        TraceDetail::CIA_IEC
    };

    for (auto d : ciaDetails)
    {
        if (enable) enableDetail(d);
        else disableDetail(d);
    }
}

void TraceManager::enableCPUDetails(bool enable)
{
    const TraceDetail cpuDetails[] =
    {
        TraceDetail::CPU_EXEC,
        TraceDetail::CPU_IRQ,
        TraceDetail::CPU_NMI,
        TraceDetail::CPU_STACK,
        TraceDetail::CPU_BRANCH,
        TraceDetail::CPU_FLAGS,
        TraceDetail::CPU_BA,
        TraceDetail::CPU_JAM
    };

    for (auto d : cpuDetails)
    {
        if (enable) enableDetail(d);
        else disableDetail(d);
    }
}

void TraceManager::enableVICDetails(bool enable)
{
    const TraceDetail vicDetails[] =
    {
        TraceDetail::VIC_RASTER,
        TraceDetail::VIC_IRQ,
        TraceDetail::VIC_REG,
        TraceDetail::VIC_BADLINE,
        TraceDetail::VIC_SPRITE,
        TraceDetail::VIC_BUS,
        TraceDetail::VIC_EVENT
    };

    for (auto d : vicDetails)
    {
        if (enable) enableDetail(d);
        else disableDetail(d);
    }
}

void TraceManager::enableAllDetails(bool enable)
{
    enableCPUDetails(enable);
    enableVICDetails(enable);
    enableCIADetails(enable);
}

void TraceManager::recordCartBank(const char* mapper, int bank, uint16_t lo, uint16_t hi, Stamp stamp)
{
    if (!tracing || !catOn(TraceCat::CART)) return;

    std::stringstream out;

    out << makeStamp(stamp) << "[CART] Mapper: " << mapper << " Bank: " << bank << " CART_LO: " << std::hex << std::setw(4) << lo << " CART_HI: " << hi;
    buffer.push_back(out.str());
    if (file.is_open()) file << buffer.back() << "\n";
}

void TraceManager::recordCiaTimer(int cia, char timerName, uint16_t value, bool underflow, Stamp stamp)
{
    if (!ciaDetailOn(cia, TraceDetail::CIA_TIMER)) return;

    std::stringstream out;
    out << makeStamp(stamp) << "[CIA" << cia << ":TIMER] "
        << "Timer=" << timerName
        << " Value=$" << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << value
        << " Underflow=" << (underflow ? "Yes" : "No");

    buffer.push_back(out.str());
    if (file.is_open()) file << buffer.back() << "\n";
}

void TraceManager::recordCiaICR(int cia, uint8_t icr, bool irqRaised, Stamp stamp)
{
    if (!ciaDetailOn(cia, TraceDetail::CIA_IRQ)) return;

    std::stringstream out;
    out << makeStamp(stamp) << "[CIA" << cia << ":IRQ] "
        << "ICR=$" << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << int(icr)
        << " Raised=" << (irqRaised ? "True" : "False");

    buffer.push_back(out.str());
    if (file.is_open()) file << buffer.back() << "\n";
}
void TraceManager::recordCPUExec(uint16_t pcExec, uint8_t opcode, Stamp stamp)
{
    if (!processor || !cpuDetailOn(TraceDetail::CPU_EXEC)) return;

    std::stringstream out;

    // Do heading first
    out << makeStamp(stamp);

    auto st = processor->getState();
    out << "[CPU]" << std::hex << std::uppercase << std::setfill('0')
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

void TraceManager::recordCPUIRQ(const std::string& text, Stamp stamp)
{
    if (!cpuDetailOn(TraceDetail::CPU_IRQ)) return;

    std::ostringstream out;
    out << makeStamp(stamp) << "[CPU:IRQ] " << text;

    buffer.push_back(out.str());
    if (file.is_open()) file << buffer.back() << "\n";
}

void TraceManager::recordCPUNMI(const std::string& text, Stamp stamp)
{
    if (!cpuDetailOn(TraceDetail::CPU_NMI)) return;

    std::ostringstream out;
    out << makeStamp(stamp) << "[CPU:NMI] " << text;

    buffer.push_back(out.str());
    if (file.is_open()) file << buffer.back() << "\n";
}

void TraceManager::recordCPUStack(const std::string& text, Stamp stamp)
{
    if (!cpuDetailOn(TraceDetail::CPU_STACK)) return;

    std::ostringstream out;
    out << makeStamp(stamp) << "[CPU:STACK] " << text;

    buffer.push_back(out.str());
    if (file.is_open()) file << buffer.back() << "\n";
}

void TraceManager::recordCPUBA(const std::string& text, Stamp stamp)
{
    if (!cpuDetailOn(TraceDetail::CPU_BA)) return;

    std::ostringstream out;
    out << makeStamp(stamp) << "[CPU:BA] " << text;

    buffer.push_back(out.str());
    if (file.is_open()) file << buffer.back() << "\n";
}

void TraceManager::recordCPUJam(const std::string& text, Stamp stamp)
{
    if (!cpuDetailOn(TraceDetail::CPU_JAM)) return;

    std::ostringstream out;
    out << makeStamp(stamp) << "[CPU:JAM] " << text;

    buffer.push_back(out.str());
    if (file.is_open()) file << buffer.back() << "\n";
}

void TraceManager::recordMemRead(uint16_t address, uint8_t value, uint16_t pc, Stamp stamp)
{
    if (!tracing || !catOn(TraceCat::MEM)) return;

    std::stringstream out;

    out << makeStamp(stamp) << "[MEMORY] READ: Address=$" << std::hex << std::uppercase << std::setfill('0')
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

    out << makeStamp(stamp) << "[MEMORY] WRITE: Address=$" << std::hex << std::uppercase << std::setfill('0')
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

    out << makeStamp(stamp) << "[PLA] Mode: " << int(mode) << " Game Line: " << (game ? "1 (inactive)" : "0 (asserted)") <<
        " exRom: " << (exrom ? "1 (inactive" : "0 (asserted)") << " CHAREN: " << charen <<  " HIRAM: " << hiram << " LORAM: " << loram;

    buffer.push_back(out.str());
    if (file.is_open()) file << buffer.back() << "\n";
}

void TraceManager::recordPlaPortWrite(uint8_t oldValue, uint8_t newValue,
                                      bool game, bool exrom, bool charen, bool hiram, bool loram,
                                      Stamp stamp)
{
    if (!tracing || !catOn(TraceCat::PLA)) return;

    std::stringstream out;
    out << makeStamp(stamp)
        << "[PLA] $0001 write old=$" << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << int(oldValue)
        << " new=$" << std::setw(2) << int(newValue)
        << " GAME=" << (game ? "1" : "0")
        << " EXROM=" << (exrom ? "1" : "0")
        << " CHAREN=" << (charen ? "1" : "0")
        << " HIRAM=" << (hiram ? "1" : "0")
        << " LORAM=" << (loram ? "1" : "0");

    buffer.push_back(out.str());
    if (file.is_open()) file << buffer.back() << "\n";
}

void TraceManager::recordPlaResolve(uint16_t address, const char* bankName, uint16_t offset, uint8_t mcr, uint8_t mode,
    bool game, bool exrom, bool charen, bool hiram, bool loram, Stamp stamp)
{
    if (!tracing || !catOn(TraceCat::PLA)) return;

    std::stringstream out;
    out << makeStamp(stamp)
        << "[PLA] resolve addr=$" << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << address
        << " -> " << bankName
        << " offset=$" << std::setw(4) << offset
        << " MCR=$" << std::setw(2) << int(mcr)
        << " mode=" << std::dec << int(mode)
        << " GAME=" << (game ? "1" : "0")
        << " EXROM=" << (exrom ? "1" : "0")
        << " CHAREN=" << (charen ? "1" : "0")
        << " HIRAM=" << (hiram ? "1" : "0")
        << " LORAM=" << (loram ? "1" : "0");

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

void TraceManager::recordVicRaster(uint16_t line, uint16_t dot, bool irq, uint8_t d011, uint8_t d012, Stamp stamp)
{
    if (!vicDetailOn(TraceDetail::VIC_RASTER)) return;

    std::stringstream out;

    out << makeStamp(stamp)
    << "[VIC] Raster Line=" << std::dec << line
    << " Dot=" << dot
    << " IRQ=" << (irq ? "ON" : "OFF")
    << " D011=$" << std::hex << std::setw(2) << std::setfill('0') << +d011
    << " D012=$" << std::setw(2) << std::setfill('0') << +d012;

    buffer.push_back(out.str());
    if (file.is_open()) file << buffer.back() << "\n";
}

void TraceManager::recordVicIrq(bool level, Stamp stamp)
{
    if (!vicDetailOn(TraceDetail::VIC_IRQ)) return;

    std::stringstream out;

    out << makeStamp(stamp) << "[VIC] IRQ Line Level: " << (level ? "High" : "Level");

    buffer.push_back(out.str());
    if (file.is_open()) file << buffer.back() << "\n";
}

void TraceManager::recordVicEvent(const std::string& text, Stamp stamp)
{
    if (!vicDetailOn(TraceDetail::VIC_EVENT)) return;

    std::stringstream out;
    out << makeStamp(stamp) << "[VIC] " << text;

    buffer.push_back(out.str());
    if (file.is_open()) file << buffer.back() << "\n";
}

void TraceManager::recordVicRegister(const std::string& text, Stamp stamp)
{
    if (!vicDetailOn(TraceDetail::VIC_REG)) return;

    std::ostringstream out;
    out << makeStamp(stamp) << "[VIC:REG] " << text;

    buffer.push_back(out.str());
    if (file.is_open()) file << buffer.back() << "\n";
}

void TraceManager::recordVicBadline(const std::string& text, Stamp stamp)
{
    if (!vicDetailOn(TraceDetail::VIC_BADLINE)) return;

    std::ostringstream out;
    out << makeStamp(stamp) << "[VIC:BADLINE] " << text;

    buffer.push_back(out.str());
    if (file.is_open()) file << buffer.back() << "\n";
}

void TraceManager::recordVicSprite(const std::string& text, Stamp stamp)
{
    if (!vicDetailOn(TraceDetail::VIC_SPRITE)) return;

    std::ostringstream out;
    out << makeStamp(stamp) << "[VIC:SPRITE] " << text;

    buffer.push_back(out.str());
    if (file.is_open()) file << buffer.back() << "\n";
}

void TraceManager::recordVicBus(const std::string& text, Stamp stamp)
{
    if (!vicDetailOn(TraceDetail::VIC_BUS)) return;

    std::ostringstream out;
    out << makeStamp(stamp) << "[VIC:BUS] " << text;

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

void TraceManager::recordCustomEvent(const std::string& text, Stamp stamp)
{
    if (!tracing) return;

    std::string line = makeStamp(stamp) + text;
    buffer.push_back(line);
    if (file.is_open()) file << line << "\n";
}

std::string TraceManager::makeStamp(const Stamp& stamp) const
{
    std::stringstream out;

    out << "[" << stamp.cycles << " " <<  std::hex << std::uppercase << std::setw(3) << std::setfill('0')
        << stamp.rasterLine << "." << std::dec << std::setw(3) << std::setfill('0') << stamp.rasterDot <<  "]";

    return out.str();
}
