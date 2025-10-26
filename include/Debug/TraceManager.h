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
        inline void attachSIDInstance(SID* sidchip) { this->sidchip = sidchip; }
        inline void attachVicInstance(Vic* vicII) { this->vicII = vicII; }

        // Getters
        inline bool isEnabled() const { return tracing; }

        // Setters
        void enable(bool on);
        void setFileOutput(const std::string& path);

        void dumpBuffer();
        void clearBuffer();

        void recordCPUTrace(uint16_t pcExec, uint8_t opcode);  // Called every CPU step if tracing enabled
        void recordCustomEvent(const std::string& text);

    protected:

    private:

        // Non-owning pointers
        CIA1* cia1object;
        CIA2* cia2object;
        CPU* processor;
        Memory* mem;
        SID* sidchip;
        Vic* vicII;

        // Status
        bool tracing = false;
        std::ofstream file;
        std::vector<std::string> buffer;
};

#endif // TRACEMANAGER_H
