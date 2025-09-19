// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef MLMONITOR_H
#define MLMONITOR_H

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include "Debug/AssembleCommand.h"
#include "Debug/BreakpointCommand.h"
#include "Debug/CartridgeCommand.h"
#include "Debug/CIACommand.h"
#include "Debug/DisassembleCommand.h"
#include "Debug/ExportDisassemblyCommand.h"
#include "Debug/GoCommand.h"
#include "Debug/JamCommand.h"
#include "Debug/MemoryDumpCommand.h"
#include "Debug/MemoryEditCommand.h"
#include "Debug/NextCommand.h"
#include "Debug/PLACommand.h"
#include "Debug/RegisterDumpCommand.h"
#include "Debug/ResetCommand.h"
#include "Debug/SIDCommand.h"
#include "Debug/StepCommand.h"
#include "Debug/VICCommand.h"
#include "Debug/WatchCommand.h"
#include "Debug/MonitorCommand.h"

// Forward declarations
class Computer;

class MLMonitor
{
    public:
        MLMonitor();
        virtual ~MLMonitor();

        void enter();  // pause emulation and enter monitor

        inline void setRunningFlag(bool flag) { running = flag; }

        void attachComputerInstance(Computer* comp);
        inline Computer* computer() const { return comp; }

        // Breakpoint management
        inline void addBreakpoint(uint16_t bp) { breakpoints.insert(bp); }
        inline void clearAllBreakpoints() { breakpoints.clear(); }
        void clearBreakpoint(uint16_t bp);
        void listBreakpoints() const;

        // Watch handling
        void addWatch(uint16_t address);
        void clearWatch(uint16_t address);
        void clearAllWatches();
        void listWatches() const;
        bool checkWatch(uint16_t address, uint8_t newVal);

        // Helpers
        inline bool breakpointsEmpty() const { return breakpoints.empty(); }
        inline bool hasBreakpoint(uint16_t pc) { return (breakpoints.find(pc) != breakpoints.end()); }
        bool isRasterWaitLoop(uint16_t pc, uint8_t& targetRaster);

    protected:

    private:

        // Pointers
        Computer* comp;
        std::unordered_map<std::string, std::unique_ptr<MonitorCommand>> commands;


        // Flag to set running state
        bool running;

        // Unordered list to hold any breakpoints set
        std::unordered_set<uint16_t> breakpoints;

        // Unordered list to hold any watches set
        std::unordered_map<uint16_t, uint8_t> watchpoints; // addr -> last value

        // Monitor helpers
        void registerCommand(std::unique_ptr<MonitorCommand> cmd);
        void handleCommand(const std::string& line);
};

#endif // MLMONITOR_H
