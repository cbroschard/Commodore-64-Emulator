// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Computer.h"
#include "DebugManager.h"
#include "MachineBuilder.h"
#include "MachineRomConfig.h"
#include "MachineComponents.h"
#include "MachineRuntimeState.h"
#include "ResetController.h"
#include "StateManager.h"
#include "UIBridge.h"

MachineBuilder::MachineBuilder() = default;

MachineBuilder::~MachineBuilder() = default;

void MachineBuilder::assemble(Computer* host, MachineComponents& components, MachineRuntimeState& runtime, MachineRomConfig& roms)
{
    // Attach components to each other
    components.debug = std::make_unique<DebugManager>(runtime.uiPaused);

    components.debug->wireBackend(host, components.cart.get(), components.cass.get(), components.cia1.get(), components.cia2.get(),
                                   components.cpu.get(), components.bus.get(), components.io.get(), components.keyb.get(),
                                   components.logger.get(), components.mem.get(), components.pla.get(), components.sid.get(), components.vic.get());

    components.debug->wireTrace(components.cart.get(), components.cia1.get(), components.cia2.get(), components.cpu.get(),
                                 components.mem.get(), components.pla.get(), components.sid.get(), components.vic.get());

    components.bus->attachCIA2Instance(components.cia2.get());
    components.bus->attachLogInstance(components.logger.get());

    components.cart->attachCPUInstance(components.cpu.get());
    components.cart->attachHostInstance(host);
    components.cart->attachMemoryInstance(components.mem.get());
    components.cart->attachLogInstance(components.logger.get());
    components.cart->attachTraceManagerInstance(&components.debug->trace());
    components.cart->attachVicInstance(components.vic.get());

    components.cass->attachMemoryInstance(components.mem.get());
    components.cass->attachLogInstance(components.logger.get());

    components.cia1->attachCassetteInstance(components.cass.get());
    components.cia1->attachCPUInstance(components.cpu.get());
    components.cia1->attachIRQLineInstance(components.irq.get());
    components.cia1->attachKeyboardInstance(components.keyb.get());
    components.cia1->attachLogInstance(components.logger.get());
    components.cia1->attachMemoryInstance(components.mem.get());
    components.cia1->attachTraceManagerInstance(&components.debug->trace());
    components.cia1->attachVicInstance(components.vic.get());

    components.cia2->attachCPUInstance(components.cpu.get());
    components.cia2->attachIECBusInstance(components.bus.get());
    components.cia2->attachLogInstance(components.logger.get());
    components.cia2->attachTraceManagerInstance(&components.debug->trace());
    components.cia2->attachVicInstance(components.vic.get());

    components.io->attachVICInstance(components.vic.get());
    components.io->attachSIDInstance(components.sid.get());
    components.io->attachLogInstance(components.logger.get());
    components.io->setMonitorOpenCallback([&components]() -> bool { return components.debug && components.debug->monitorController().isOpen();});

    components.keyb->attachLogInstance(components.logger.get());

    components.mem->attachCPUInstance(components.cpu.get());
    components.mem->attachVICInstance(components.vic.get());
    components.mem->attachCIA1Instance(components.cia1.get());
    components.mem->attachCIA2Instance(components.cia2.get());
    components.mem->attachSIDInstance(components.sid.get());
    components.mem->attachCartridgeInstance(components.cart.get());
    components.mem->attachCassetteInstance(components.cass.get());
    components.mem->attachPLAInstance(components.pla.get());
    components.mem->attachMonitorInstance(&components.debug->monitor());
    components.mem->attachLogInstance(components.logger.get());
    components.mem->attachTraceManagerInstance(&components.debug->trace());

    components.inputMgr->attachCIA1Instance(components.cia1.get());
    components.inputMgr->attachKeyboardInstance(components.keyb.get());
    components.inputMgr->attachMonitorControllerInstance(&components.debug->monitorController());

    components.pla->attachCartridgeInstance(components.cart.get());
    components.pla->attachCPUInstance(components.cpu.get());
    components.pla->attachLogInstance(components.logger.get());
    components.pla->attachTraceManagerInstance(&components.debug->trace());
    components.pla->attachVICInstance(components.vic.get());

    components.cpu->attachMemoryInstance(components.mem.get());
    components.cpu->attachCIA2Instance(components.cia2.get());
    components.cpu->attachVICInstance(components.vic.get());
    components.cpu->attachIRQLineInstance(components.irq.get());
    components.cpu->attachLogInstance(components.logger.get());
    components.cpu->attachTraceManagerInstance(&components.debug->trace());

    components.sid->attachCPUInstance(components.cpu.get());
    components.sid->attachLogInstance(components.logger.get());
    components.sid->attachTraceManagerInstance(&components.debug->trace());
    components.sid->attachVicInstance(components.vic.get());

    components.vic->attachIOInstance(components.io.get());
    components.vic->attachCPUInstance(components.cpu.get());
    components.vic->attachMemoryInstance(components.mem.get());
    components.vic->attachCIA2Instance(components.cia2.get());
    components.vic->attachIRQLineInstance(components.irq.get());
    components.vic->attachLogInstance(components.logger.get());
    components.vic->attachTraceManagerInstance(&components.debug->trace());

    components.media = std::make_unique<MediaManager>(components.cart, components.drives, host, *components.bus, *components.mem, *components.pla,
                                                       *components.cpu, *components.vic, components.debug->backend(), components.debug->trace(),
                                                       *components.cass, *components.logger, roms.d1541LoRom, roms.d1541HiRom, roms.d1571Rom,
                                                       roms.d1581Rom, [&pendingBusPrime = runtime.pendingBusPrime,
                                                       &busPrimedAfterBoot = runtime.busPrimedAfterBoot]()
                                                       { if (!busPrimedAfterBoot) pendingBusPrime = true; }, [host]() { host->coldReset(); });

    if (components.media) components.media->setVideoMode(runtime.videoMode);

    components.inputRouter = std::make_unique<InputRouter>(runtime.uiPaused, &components.debug->monitorController(), components.inputMgr.get(),
                                                            components.media.get(), [host]() { host->warmReset(); }, [host]() { host->coldReset(); },
                                                            [&components]() { if (components.uiBridge) components.uiBridge->toggleManualPause(); });

    components.resetCtl = std::make_unique<ResetController>(*components.cpu, *components.mem, *components.pla, *components.cia1, *components.cia2,
                                                             *components.vic, *components.sid, *components.bus, *components.cart,
                                                             components.media.get(), roms.basicRom, roms.kernalRom, roms.charRom, runtime.videoMode,
                                                             runtime.cpuCfg);

    components.uiBridge = std::make_unique<UIBridge>(*components.ui, components.media.get(), components.inputMgr.get(), runtime.uiPaused,
                                                      runtime.running, [host](const std::string& p) { host->saveStateToFile(p); },
                                                      [host](const std::string& p) { host->loadStateFromFile(p); }, [host]() { host->warmReset(); },
                                                      [host]() { host->coldReset(); }, [host](const std::string& mode) { host->setVideoMode(mode); },
                                                      [host]() { host->enterMonitor(); },
                                                      [&videoMode = runtime.videoMode]() -> bool { return videoMode == VideoMode::PAL; },
                                                      [&components]() -> bool {return components.debug && components.debug->monitorController().isOpen();});

    components.stateMgr = std::make_unique<StateManager>(*components.cart, *components.cass, *components.cia1, *components.cia2,
                                                          *components.cpu, *components.bus, *components.inputMgr, *components.logger,
                                                          *components.media, *components.mem, *components.pla, *components.sid, *components.vic,
                                                          runtime.uiPaused, runtime.videoMode, runtime.cpuCfg, runtime.pendingBusPrime,
                                                          runtime.busPrimedAfterBoot, components.drives);
}
