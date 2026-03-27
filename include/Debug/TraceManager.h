// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef TRACEMANAGER_H
#define TRACEMANAGER_H

#include <bit>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <memory>

// Forward declarations
class Cartridge;
class CIA1;
class CIA2;
class CPU;
class Memory;
class PLA;
class SID;
class Vic;

class TraceManager
{
    public:
        TraceManager();
        virtual ~TraceManager();

        inline void attachCartInstance(Cartridge* cart) { this->cart = cart; }
        inline void attachCIA1Instance(CIA1* cia1object) { this->cia1object = cia1object; }
        inline void attachCIA2Instance(CIA2* cia2object) { this->cia2object = cia2object; }
        inline void attachCPUInstance(CPU* processor) { this->processor = processor; }
        inline void attachMemoryInstance(Memory* mem) { this->mem = mem; }
        inline void attachPLAInstance(PLA* pla) { this->pla = pla; }
        inline void attachSIDInstance(SID* sidchip) { this->sidchip = sidchip; }
        inline void attachVicInstance(Vic* vicII) { this->vicII = vicII; }

        enum class TraceCat : uint32_t
        {
            CPU   = 1u<<0,
            VIC   = 1u<<1,
            CIA1  = 1u<<2,
            CIA2  = 1u<<3,
            PLA   = 1u<<4,
            SID   = 1u<<5,
            CART  = 1u<<6,
            MEM   = 1u<<7,
            COUNT
        };

        enum class TraceDetail : uint64_t
        {
            // CPU
            CPU_EXEC    = 1ull << 0,
            CPU_IRQ     = 1ull << 1,
            CPU_NMI     = 1ull << 2,
            CPU_STACK   = 1ull << 3,
            CPU_BRANCH  = 1ull << 4,
            CPU_FLAGS   = 1ull << 5,
            CPU_BA      = 1ull << 6,
            CPU_JAM     = 1ull << 7,

            // VIC
            VIC_RASTER  = 1ull << 8,
            VIC_IRQ     = 1ull << 9,
            VIC_REG     = 1ull << 10,
            VIC_BADLINE = 1ull << 11,
            VIC_SPRITE  = 1ull << 12,
            VIC_BUS     = 1ull << 13,
            VIC_EVENT   = 1ull << 14,
        };

        // Getters
        inline bool isEnabled() const { return tracing; }
        inline bool catOn(TraceCat c) const { return chipOn(c); }
        inline bool memRangesIsEmpty() const { return memRanges.empty(); }
        inline bool detailEnabled(TraceDetail d) const { return detailOn(d); }
        bool cpuDetailOn(TraceDetail d) const;
        bool vicDetailOn(TraceDetail d) const;
        std::string listDetailStatus() const;
        std::string listCategoryStatus();
        bool memRangeContains(uint16_t address) const;
        std::string listMemRange() const;

        // Setters
        inline void enableCategory(TraceCat cat) { chipCats |= catToMask(cat); }
        inline void disableCategory(TraceCat cat) { chipCats &= ~catToMask(cat); }
        inline void addMemRange(uint16_t lo, uint16_t hi) { memRanges.push_back({lo,hi}); }
        inline void enableDetail(TraceDetail d) { detailCats |= detailToMask(d); }
        inline void disableDetail(TraceDetail d) { detailCats &= ~detailToMask(d); }

        void enableAllDetails(bool enable);
        void enableCPUDetails(bool enable);
        void enableVICDetails(bool enable);
        void enableAllCategories(bool enable);
        void enable(bool on);
        void setFileOutput(const std::string& path);


        // Helpers
        inline void clearMemRanges() { memRanges.clear(); }
        void dumpBuffer();
        void clearBuffer();

        // Standard stamping for logging
        struct Stamp
        {
            uint64_t cycles;      // CPU totalCycles
            uint16_t rasterLine;  // VIC line if available
            uint16_t rasterDot;   // VIC dot if available
        };
        inline Stamp makeStamp(uint64_t cyc, uint16_t rl=0xFFFF, uint16_t rd=0xFFFF) { return {cyc, rl, rd}; }

        // Component specific traces

        // Cartridge
        void recordCartBank(const char* mapper, int bank, uint16_t lo, uint16_t hi, Stamp stamp);

        // CPU
        void recordCPUExec(uint16_t pcExec, uint8_t opcode, Stamp stamp);
        void recordCPUIRQ(const std::string& text, Stamp stamp);
        void recordCPUNMI(const std::string& text, Stamp stamp);
        void recordCPUStack(const std::string& text, Stamp stamp);
        void recordCPUBA(const std::string& text, Stamp stamp);
        void recordCPUJam(const std::string& text, Stamp stamp);

        // CIA
        void recordCiaTimer(int cia, char timerName, uint16_t value, bool underflow, Stamp stamp);
        void recordCiaICR(int cia, uint8_t icr, bool irqRaised, Stamp stamp);

        // Memory
        void recordMemRead(uint16_t address, uint8_t value, uint16_t pc, Stamp stamp);
        void recordMemWrite(uint16_t address, uint8_t value, uint16_t pc, Stamp stamp);

        // PLA
        void recordPlaMode(uint8_t mode, bool game, bool exrom, bool charen, bool hiram, bool loram, Stamp stamp);

        // SID
        void recordSidWrite(uint16_t reg, uint8_t val, Stamp stamp);

        // VIC
        void recordVicRaster(uint16_t line, uint16_t dot, bool irq, uint8_t d011, uint8_t d012, Stamp stamp);
        void recordVicIrq(bool level, Stamp stamp);
        void recordVicEvent(const std::string& text, Stamp stamp);
        void recordVicRegister(const std::string& text, Stamp stamp);
        void recordVicBadline(const std::string& text, Stamp stamp);
        void recordVicSprite(const std::string& text, Stamp stamp);
        void recordVicBus(const std::string& text, Stamp stamp);

        // Other/Custom
        void recordCustomEvent(const std::string& text);

    protected:

    private:
        // Non-owning pointers
        Cartridge* cart;
        CIA1* cia1object;
        CIA2* cia2object;
        CPU* processor;
        Memory* mem;
        PLA* pla;
        SID* sidchip;
        Vic* vicII;

        // Status
        bool tracing;
        std::ofstream file;
        std::vector<std::string> buffer;

        // Categories
        uint32_t chipCats;
        uint64_t detailCats;

        static constexpr std::pair<TraceCat, const char*> catNames[] =
        {
            { TraceCat::CPU, "CPU" },
            { TraceCat::VIC, "VIC" },
            { TraceCat::CIA1, "CIA1" },
            { TraceCat::CIA2, "CIA2" },
            { TraceCat::PLA, "PLA" },
            { TraceCat::SID, "SID" },
            { TraceCat::CART, "CART" },
            { TraceCat::MEM, "MEM" },
        };

        // SID register names
        static const char* sidRegNames[32];

        // Helpers
        inline uint32_t categories() const { return chipCats; }
        inline uint32_t catToMask(TraceCat cat) const { return static_cast<uint32_t>(cat); }
        inline uint64_t detailToMask(TraceDetail detail) const { return static_cast<uint64_t>(detail); }
        inline bool chipOn(TraceCat c) const { return (chipCats & catToMask(c)) != 0; }
        inline bool detailOn(TraceDetail d) const { return (detailCats & detailToMask(d)) != 0; }

        struct AddrRange { uint16_t lo, hi; bool contains(uint16_t a) const { return a>=lo && a<=hi; } };
        std::vector<AddrRange> memRanges;
        std::string makeStamp(const Stamp& stamp) const;
};

#endif // TRACEMANAGER_H
