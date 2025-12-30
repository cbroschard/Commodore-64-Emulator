// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef MLMONITOR_H
#define MLMONITOR_H

#include <algorithm>
#include <iostream>
#include <memory>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include "Debug/AssembleCommand.h"
#include "Debug/BreakpointCommand.h"
#include "Debug/CartridgeCommand.h"
#include "Debug/CIACommand.h"
#include "Debug/DisassembleCommand.h"
#include "Debug/DriveCommand.h"
#include "Debug/ExportDisassemblyCommand.h"
#include "Debug/GoCommand.h"
#include "Debug/IECCommand.h"
#include "Debug/IRQCommand.h"
#include "Debug/JamCommand.h"
#include "Debug/LogCommand.h"
#include "Debug/MemoryDumpCommand.h"
#include "Debug/MemoryEditCommand.h"
#include "Debug/MemoryEditDirectCommand.h"
#include "Debug/NextCommand.h"
#include "Debug/PLACommand.h"
#include "Debug/RegisterDumpCommand.h"
#include "Debug/ResetCommand.h"
#include "Debug/SIDCommand.h"
#include "Debug/StepCommand.h"
#include "Debug/TapeCommand.h"
#include "Debug/TraceCommand.h"
#include "Debug/VICCommand.h"
#include "Debug/WatchCommand.h"
#include "Debug/MonitorCommand.h"
#include "imgui/imgui.h"

// Forward declarations
class Computer;
class MLMonitorBackend;

class MLMonitor
{
    public:
        MLMonitor();
        virtual ~MLMonitor();

        void enter();  // pause emulation and enter monitor
        void draw(bool* p_open);
        void addLog(const char* fmt, ...);
        void execCommand(const char* command_line);

        inline void setRunningFlag(bool flag) { running = flag; }

        inline void attachMLMonitorBackendInstance(MLMonitorBackend* monbackend) { this->monbackend = monbackend; }
        inline MLMonitorBackend* mlmonitorbackend() const { return monbackend; }
        void attachTraceManagerInstance(class TraceManager* tm);

        // Breakpoint management
        inline void addBreakpoint(uint16_t bp) { breakpoints.insert(bp); }
        inline void clearAllBreakpoints() { breakpoints.clear(); }
        void clearBreakpoint(uint16_t bp);
        void listBreakpoints() const;

        // Watch write handling
        void addWriteWatch(uint16_t address);
        void clearWriteWatch(uint16_t address);
        void clearAllWriteWatches();
        void listWriteWatches() const;
        bool checkWatchWrite(uint16_t address, uint8_t newVal);
        std::vector<uint16_t> getWriteWatchAddresses() const;

        // Watch read handling
        void addReadWatch(uint16_t address);
        void clearReadWatch(uint16_t address);
        void clearAllReadWatches();
        void listReadWatches() const;
        bool checkWatchRead(uint16_t address, uint8_t value);
        std::vector<uint16_t> getReadWatchAddresses() const;

        // Helpers
        inline bool breakpointsEmpty() const { return breakpoints.empty(); }
        inline bool hasBreakpoint(uint16_t pc) { return (breakpoints.find(pc) != breakpoints.end()); }
        bool isRasterWaitLoop(uint16_t pc, uint8_t& targetRaster);

    protected:

    private:

        // Pointers
        MLMonitorBackend* monbackend;

        std::unordered_map<std::string, std::unique_ptr<MonitorCommand>> commands;

        // Flag to set running state
        bool running;

        // Unordered list to hold any breakpoints set
        std::unordered_set<uint16_t> breakpoints;

        // Unordered list to hold any watches set
        std::unordered_map<uint16_t, uint8_t> writeWatches; // addr -> last value
        std::unordered_set<uint16_t> readWatches;

        // ImGui Console State
        ImGuiTextBuffer Items;
        std::vector<int> LineOffsets; // Index to lines offset.
        bool AutoScroll;
        bool ScrollToBottom;
        char InputBuf[256];
        std::vector<std::string> History;
        int HistoryPos; // -1: new line, 0..History.size()-1 browsing history.

        // Monitor helpers
        void registerCommand(std::unique_ptr<MonitorCommand> cmd);
        void handleCommand(const std::string& line);
        void captureOutputAndExecute(const std::string& cmdLine);
};

#endif // MLMONITOR_H
