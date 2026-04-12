// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "DebugManager.h"
#include "MachineBuilder.h"
#include "MachineRomConfig.h"
#include "MachineRuntimeState.h"
#include "ResetController.h"
#include "StateManager.h"
#include "UIBridge.h"

MachineBuilder::MachineBuilder() = default;

MachineBuilder::~MachineBuilder() = default;

void MachineBuilder::assemble(Computer* host, MachineComponents& components_, MachineRuntimeState& runtime, MachineRomConfig& roms)
{
    // Attach components to each other
    components_.debug = std::make_unique<DebugManager>(runtime.uiPaused);

    components_.debug->wireBackend(host, components_.cart.get(), components_.cass.get(), components_.cia1.get(), components_.cia2.get(),
                                   components_.cpu.get(), components_.bus.get(), components_.io.get(), components_.keyb.get(),
                                   components_.logger.get(), components_.mem.get(), components_.pla.get(), components_.sid.get(), components_.vic.get());

    components_.debug->wireTrace(components_.cart.get(), components_.cia1.get(), components_.cia2.get(), components_.cpu.get(),
                                 components_.mem.get(), components_.pla.get(), components_.sid.get(), components_.vic.get());

    components_.bus->attachCIA2Instance(components_.cia2.get());
    components_.bus->attachLogInstance(components_.logger.get());

    components_.cart->attachCPUInstance(components_.cpu.get());
    components_.cart->attachHostInstance(host);
    components_.cart->attachMemoryInstance(components_.mem.get());
    components_.cart->attachLogInstance(components_.logger.get());
    components_.cart->attachTraceManagerInstance(&components_.debug->trace());
    components_.cart->attachVicInstance(components_.vic.get());

    components_.cass->attachMemoryInstance(components_.mem.get());
    components_.cass->attachLogInstance(components_.logger.get());

    components_.cia1->attachCassetteInstance(components_.cass.get());
    components_.cia1->attachCPUInstance(components_.cpu.get());
    components_.cia1->attachIRQLineInstance(components_.irq.get());
    components_.cia1->attachKeyboardInstance(components_.keyb.get());
    components_.cia1->attachLogInstance(components_.logger.get());
    components_.cia1->attachMemoryInstance(components_.mem.get());
    components_.cia1->attachTraceManagerInstance(&components_.debug->trace());
    components_.cia1->attachVicInstance(components_.vic.get());

    components_.cia2->attachCPUInstance(components_.cpu.get());
    components_.cia2->attachIECBusInstance(components_.bus.get());
    components_.cia2->attachLogInstance(components_.logger.get());
    components_.cia2->attachTraceManagerInstance(&components_.debug->trace());
    components_.cia2->attachVicInstance(components_.vic.get());

    components_.io->attachVICInstance(components_.vic.get());
    components_.io->attachSIDInstance(components_.sid.get());
    components_.io->attachLogInstance(components_.logger.get());
    components_.io->setMonitorOpenCallback([&components_]() -> bool { return components_.debug && components_.debug->monitorController().isOpen();});

    components_.keyb->attachLogInstance(components_.logger.get());

    components_.mem->attachCPUInstance(components_.cpu.get());
    components_.mem->attachVICInstance(components_.vic.get());
    components_.mem->attachCIA1Instance(components_.cia1.get());
    components_.mem->attachCIA2Instance(components_.cia2.get());
    components_.mem->attachSIDInstance(components_.sid.get());
    components_.mem->attachCartridgeInstance(components_.cart.get());
    components_.mem->attachCassetteInstance(components_.cass.get());
    components_.mem->attachPLAInstance(components_.pla.get());
    components_.mem->attachMonitorInstance(&components_.debug->monitor());
    components_.mem->attachLogInstance(components_.logger.get());
    components_.mem->attachTraceManagerInstance(&components_.debug->trace());

    components_.inputMgr->attachCIA1Instance(components_.cia1.get());
    components_.inputMgr->attachKeyboardInstance(components_.keyb.get());
    components_.inputMgr->attachMonitorControllerInstance(&components_.debug->monitorController());

    components_.pla->attachCartridgeInstance(components_.cart.get());
    components_.pla->attachCPUInstance(components_.cpu.get());
    components_.pla->attachLogInstance(components_.logger.get());
    components_.pla->attachTraceManagerInstance(&components_.debug->trace());
    components_.pla->attachVICInstance(components_.vic.get());

    components_.cpu->attachMemoryInstance(components_.mem.get());
    components_.cpu->attachCIA2Instance(components_.cia2.get());
    components_.cpu->attachVICInstance(components_.vic.get());
    components_.cpu->attachIRQLineInstance(components_.irq.get());
    components_.cpu->attachLogInstance(components_.logger.get());
    components_.cpu->attachTraceManagerInstance(&components_.debug->trace());

    components_.sid->attachCPUInstance(components_.cpu.get());
    components_.sid->attachLogInstance(components_.logger.get());
    components_.sid->attachTraceManagerInstance(&components_.debug->trace());
    components_.sid->attachVicInstance(components_.vic.get());

    components_.vic->attachIOInstance(components_.io.get());
    components_.vic->attachCPUInstance(components_.cpu.get());
    components_.vic->attachMemoryInstance(components_.mem.get());
    components_.vic->attachCIA2Instance(components_.cia2.get());
    components_.vic->attachIRQLineInstance(components_.irq.get());
    components_.vic->attachLogInstance(components_.logger.get());
    components_.vic->attachTraceManagerInstance(&components_.debug->trace());

    components_.media = std::make_unique<MediaManager>(components_.cart, components_.drives, host, *components_.bus, *components_.mem, *components_.pla,
                                                       *components_.cpu, *components_.vic, components_.debug->backend(), components_.debug->trace(),
                                                       *components_.cass, *components_.logger, roms.d1541LoRom, roms.d1541HiRom, roms.d1571Rom,
                                                       roms.d1581Rom, [&runtime]() { if (!runtime.busPrimedAfterBoot) runtime.pendingBusPrime = true; },
                                                       [host]() { host->coldReset(); });

    if (components_.media) components_.media->setVideoMode(runtime.videoMode);

    if (components_.media) components_.media->setVideoMode(runtime.videoMode);

    components_.inputRouter = std::make_unique<InputRouter>(runtime.uiPaused, &components_.debug->monitorController(), components_.inputMgr.get(),
                                                            components_.media.get(), [host]() { host->warmReset(); }, [host]() { host->coldReset(); },
                                                            [&components_]() { if (components_.uiBridge) components_.uiBridge->toggleManualPause(); });

    components_.resetCtl = std::make_unique<ResetController>(*components_.cpu, *components_.mem, *components_.pla, *components_.cia1, *components_.cia2,
                                                             *components_.vic, *components_.sid, *components_.bus, *components_.cart,
                                                             components_.media.get(), roms.basicRom, roms.kernalRom, roms.charRom, runtime.videoMode,
                                                             runtime.cpuCfg);

    components_.uiBridge = std::make_unique<UIBridge>(*components_.ui, components_.media.get(), components_.inputMgr.get(), runtime.uiPaused,
                                                      runtime.running, [host](const std::string& p) { host->saveStateToFile(p); },
                                                      [host](const std::string& p) { host->loadStateFromFile(p); }, [host]() { host->warmReset(); },
                                                      [host]() { host->coldReset(); }, [host](const std::string& mode) { host->setVideoMode(mode); },
                                                      [host]() { host->enterMonitor(); }, [&runtime]() -> bool { return runtime.videoMode == VideoMode::PAL; },
                                                      [&components_]() -> bool {return components_.debug && components_.debug->monitorController().isOpen();});

    components_.stateMgr = std::make_unique<StateManager>(*components_.cart, *components_.cass, *components_.cia1, *components_.cia2,
                                                          *components_.cpu, *components_.bus, *components_.inputMgr, *components_.logger,
                                                          *components_.media, *components_.mem, *components_.pla, *components_.sid, *components_.vic,
                                                          runtime.uiPaused, runtime.videoMode, runtime.cpuCfg, runtime.pendingBusPrime,
                                                          runtime.busPrimedAfterBoot, components_.drives);
}
