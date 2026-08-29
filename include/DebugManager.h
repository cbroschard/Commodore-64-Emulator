// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef DEBUGMANAGER_H
#define DEBUGMANAGER_H

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

union SDL_Event;

class Bus;
class Computer;
class Cartridge;
class Cassette;
class CIA1;
class CIA2;
class CPU;
class CPUBus;
class ExecutionHistory;
class IECBUS;
class IO;
class IRQLine;
class Keyboard;
class Memory;
class PLA;
class REU;
class SID;
class UserPort;
class Vic;

class MLMonitor;
class MLMonitorBackend;
class TraceManager;
class MonitorController;

class DebugManager
{
    public:
        explicit DebugManager(std::atomic<bool>& uiPausedRef);
        virtual ~DebugManager();

        // Monitor window control
        void openMonitor();
        void toggleMonitor();
        void closeMonitor();

        void wireBackend(Bus* c64Bus,
                         Computer* computer,
                         Cartridge* cart,
                         Cassette* cass,
                         CIA1* cia1,
                         CIA2* cia2,
                         CPU* cpu,
                         CPUBus* bus,
                         ExecutionHistory* executionHistory,
                         IECBUS* iecBus,
                         IRQLine* irq,
                         Keyboard* keyb,
                         PLA* pla,
                         REU* reu,
                         SID* sid,
                         UserPort* userPort,
                         Vic* vic);

        void wireTrace(Cartridge* cart,
                       CIA1* cia1,
                       CIA2* cia2,
                       CPU* cpu,
                       Memory* mem,
                       PLA* pla,
                       SID* sid,
                       Vic* vic);

        bool hasBreakpoint(uint16_t pc) const;
        bool onBreakpoint(uint16_t pc);   // queues message + opens monitor window

        bool onWatchpoint();

        // UI forwarding helpers
        void tick();
        bool handleEvent(const SDL_Event& ev);

        // Accessors for other systems
        MLMonitor& monitor();
        MLMonitorBackend& backend();
        TraceManager& trace();
        MonitorController& monitorController();

    private:
        std::atomic<bool>& uiPaused_;

        std::unique_ptr<MLMonitor>        monitor_;
        std::unique_ptr<MLMonitorBackend> backend_;
        std::unique_ptr<TraceManager>     trace_;
        std::unique_ptr<MonitorController> monitorCtl_;

        bool backendWired_;
        bool traceWired_;
};

#endif // DEBUGMANAGER_H
