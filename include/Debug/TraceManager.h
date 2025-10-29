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
            MEM   = 1u<<7
        };

        // Getters
        inline bool isEnabled() const { return tracing; }
        inline uint32_t categories() const { return cats; }
        inline bool catOn(TraceCat c) { return (cats & catToMask(c)) != 0; }
        inline bool memRangesIsEmpty() const { return memRanges.empty(); }
        bool memRangeContains(uint16_t address) const;
        std::string listMemRange() const;

        // Setters
        inline void enableCategory(TraceCat cat) { cats |= catToMask(cat); }
        inline void disableCategory(TraceCat cat) { cats &= ~catToMask(cat); }
        inline void addMemRange(uint16_t lo, uint16_t hi) { memRanges.push_back({lo,hi}); }
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
        void recordCPUTrace(uint16_t pcExec, uint8_t opcode, Stamp stamp);  // Called every CPU step if tracing enabled
        void recordMemRead(uint16_t address, uint8_t value, uint16_t pc, Stamp stamp);
        void recordMemWrite(uint16_t address, uint8_t value, uint16_t pc, Stamp stamp);
        void recordVicRaster(uint16_t line, uint16_t dot, bool irq, uint8_t d011, uint8_t d012, Stamp stamp);
        void recordVicIrq(bool level, Stamp stamp);
        void recordCiaTimer(int cia, char timerName, uint16_t value, bool underflow, Stamp stamp);
        void recordCiaICR(int cia, uint8_t icr, bool irqRaised, Stamp stamp);
        void recordPlaMode(uint8_t mode, bool game, bool exrom, bool charen, bool hiram, bool loram, Stamp stamp);
        void recordCartBank(const char* mapper, int bank, uint16_t lo, uint16_t hi, Stamp stamp);
        void recordSidWrite(uint16_t reg, uint8_t val, Stamp stamp);
        void recordCustomEvent(const std::string& text);

    protected:

    private:

        // Non-owning pointers
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
        uint32_t cats;

        // SID register names
        static const char* sidRegNames[32];

        // Helpers
        struct AddrRange { uint16_t lo, hi; bool contains(uint16_t a) const { return a>=lo && a<=hi; } };
        std::vector<AddrRange> memRanges;
        std::string makeStamp(const Stamp& stamp) const;
        uint32_t catToMask(TraceCat cat);


};

#endif // TRACEMANAGER_H
