// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "DebugManager.h"

#include <cstdio>

#include "Debug/MLMonitor.h"
#include "Debug/MLMonitorBackend.h"
#include "Debug/TraceManager.h"
#include "MonitorController.h"

#include "Cartridge.h"
#include "cassette.h"
#include "CIA1.h"
#include "CIA2.h"
#include "CPU.h"
#include "IECBUS.h"
#include "IO.h"
#include "keyboard.h"
#include "Logging.h"
#include "Memory.h"
#include "PLA.h"
#include "SID/SID.h"
#include "Vic.h"
#include "computer.h"

DebugManager::DebugManager(std::atomic<bool>& uiPausedRef)
    : uiPaused_(uiPausedRef),
      monitor_(std::make_unique<MLMonitor>()),
      backend_(std::make_unique<MLMonitorBackend>()),
      trace_(std::make_unique<TraceManager>()),
      monitorCtl_(std::make_unique<MonitorController>(uiPausedRef)),
      backendWired_(false),
      traceWired_(false)
{
    // Monitor window controller needs the monitor
    monitorCtl_->attachMonitorInstance(monitor_.get());

    // Monitor needs its backend (and trace manager for the "trace" command)
    monitor_->attachMLMonitorBackendInstance(backend_.get());
    monitor_->attachTraceManagerInstance(trace_.get());
}

DebugManager::~DebugManager() = default;

void DebugManager::openMonitor()
{
    if (monitorCtl_) monitorCtl_->open();
}

void DebugManager::toggleMonitor()
{
    if (monitorCtl_) monitorCtl_->toggle();
}

void DebugManager::closeMonitor()
{
    if (monitorCtl_) monitorCtl_->close();
}

void DebugManager::wireBackend(Computer* computer,
                              Cartridge* cart,
                              Cassette* cass,
                              CIA1* cia1,
                              CIA2* cia2,
                              CPU* cpu,
                              IECBUS* bus,
                              IO* io,
                              Keyboard* keyb,
                              Logging* log,
                              Memory* mem,
                              PLA* pla,
                              SID* sid,
                              Vic* vic)
{
    if (backendWired_) return;
    backendWired_ = true;

    backend_->attachCartridgeInstance(cart);
    backend_->attachCassetteInstance(cass);
    backend_->attachCIA1Instance(cia1);
    backend_->attachCIA2Instance(cia2);
    backend_->attachComputerInstance(computer);
    backend_->attachProcessorInstance(cpu);
    backend_->attachIECBusInstance(bus);
    backend_->attachIOInstance(io);
    backend_->attachKeyboardInstance(keyb);
    backend_->attachLogInstance(log);
    backend_->attachMemoryInstance(mem);
    backend_->attachPLAInstance(pla);
    backend_->attachSIDInstance(sid);
    backend_->attachVICInstance(vic);
}

void DebugManager::wireTrace(Cartridge* cart,
                            CIA1* cia1,
                            CIA2* cia2,
                            CPU* cpu,
                            Memory* mem,
                            PLA* pla,
                            SID* sid,
                            Vic* vic)
{
    if (traceWired_) return;
    traceWired_ = true;

    trace_->attachCartInstance(cart);
    trace_->attachCIA1Instance(cia1);
    trace_->attachCIA2Instance(cia2);
    trace_->attachCPUInstance(cpu);
    trace_->attachMemoryInstance(mem);
    trace_->attachPLAInstance(pla);
    trace_->attachSIDInstance(sid);
    trace_->attachVicInstance(vic);
}

bool DebugManager::hasBreakpoint(uint16_t pc) const
{
    return monitor_ && monitor_->hasBreakpoint(pc);
}

bool DebugManager::onBreakpoint(uint16_t pc)
{
    if (!hasBreakpoint(pc))
        return false;

    char msg[64];
    std::snprintf(msg, sizeof(msg), ">>> Breakpoint hit at $%04X", pc);

    // Queue first, then open (open() drains queued lines)
    monitor_->queueAsyncLine(msg);

    if (monitorCtl_)
        monitorCtl_->open();

    return true;
}

void DebugManager::tick()
{
    if (monitorCtl_)
        monitorCtl_->tick();
}

bool DebugManager::handleEvent(const SDL_Event& ev)
{
    return monitorCtl_ ? monitorCtl_->handleEvent(ev) : false;
}

MLMonitor& DebugManager::monitor()
{
    return *monitor_;
}

MLMonitorBackend& DebugManager::backend()
{
    return *backend_;
}

TraceManager& DebugManager::trace()
{
    return *trace_;
}

MonitorController& DebugManager::monitorController()
{
    return *monitorCtl_;
}
