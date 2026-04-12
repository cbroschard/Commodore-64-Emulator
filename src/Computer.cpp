// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include <chrono>
#include <filesystem>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>
#include "Computer.h"
#include "DebugManager.h"
#include "Drive/D1541.h"
#include "Drive/D1571.h"
#include "Drive/D1581.h"
#include "Drive/Drive.h"
#include "EmulationSession.h"
#include "ResetController.h"
#include "StateManager.h"
#include "Tape/TapeImageFactory.h"
#include "UIBridge.h"

Computer::Computer() :
    videoMode_(VideoMode::NTSC),
    cpuCfg_(&NTSC_CPU),
    running(true),
    uiQuit(false),
    uiPaused(false),
    pendingBusPrime(false),
    busPrimedAfterBoot(false)
{
    components_.cart = std::make_unique<Cartridge>();
    components_.cass = std::make_unique<Cassette>();
    components_.cia1 = std::make_unique<CIA1>();
    components_.cia2 = std::make_unique<CIA2>();
    components_.cpu = std::make_unique<CPU>();
    components_.ui = std::make_unique<EmulatorUI>();
    components_.bus = std::make_unique<IECBUS>();
    components_.inputMgr = std::make_unique<InputManager>();
    components_.irq = std::make_unique<IRQLine>();
    components_.keyb = std::make_unique<Keyboard>();
    components_.logger = std::make_unique<Logging>("debug.txt");
    components_.mem = std::make_unique<Memory>();
    components_.pla = std::make_unique<PLA>();
    components_.sid = std::make_unique<SID>(44100);
    components_.io = std::make_unique<IO>();
    components_.vic = std::make_unique<Vic>();

    // Wire components
    wireUp();
}

Computer::~Computer() noexcept
{
    try
    {
        if (components_.io)
        {
            // Ensure the render thread is down
            if (components_.debug) components_.debug->closeMonitor();
            running = false;
            components_.io->stopRenderThread(running);
            components_.io->setGuiCallback({});
            components_.io->setInputCallback({});

            // Audio shutdown
            components_.io->stopAudio();
        }

        if (components_.logger) components_.logger->flush();
    }
    catch(...)
    {

    }
}

bool Computer::saveStateToFile(const std::string& path)
{
    return components_.stateMgr ? components_.stateMgr->save(path) : false;
}

bool Computer::loadStateFromFile(const std::string& path)
{
    return components_.stateMgr ? components_.stateMgr->load(path) : false;
}

void Computer::requestColdReset()
{
    if (components_.resetCtl)
        components_.resetCtl->coldReset();
}

void Computer::requestWarmReset()
{
    if (components_.resetCtl)
        components_.resetCtl->warmReset();
}

void Computer::requestCartridgeNMI()
{
    if (components_.cpu)
        components_.cpu->pulseNMI();
}

void Computer::setJoystickAttached(int port, bool flag)
{
    if (components_.inputMgr) components_.inputMgr->setJoystickAttached(port, flag);
}

void Computer::set1541LoROM(const std::string& loROM)
{
    D1541LoROM = loROM;
    if (components_.media) components_.media->setD1541LoROM(loROM);
}

void Computer::set1541HiROM(const std::string& hiROM)
{
    D1541HiROM = hiROM;
    if (components_.media) components_.media->setD1541HiROM(hiROM);
}

void Computer::set1571ROM(const std::string& rom)
{
    D1571ROM = rom;
    if (components_.media) components_.media->setD1571ROM(rom);
}

void Computer::set1581ROM(const std::string& rom)
{
    D1581ROM = rom;
    if (components_.media) components_.media->setD1581ROM(rom);
}

void Computer::enterMonitor()
{
    if (components_.debug) components_.debug->openMonitor();
}

void Computer::setJoystickConfig(int port, JoystickMapping& cfg)
{
    if (!components_.inputMgr) return;
    components_.inputMgr->setJoystickConfig(port, cfg);
}

bool Computer::boot()
{
    EmulationSession session(*components_.cart,
                             *components_.cia1,
                             *components_.cia2,
                             *components_.cpu,
                             *components_.debug,
                             *components_.ui,
                             *components_.bus,
                             *components_.inputMgr,
                             *components_.inputRouter,
                             *components_.io,
                             *components_.logger,
                             *components_.media,
                             *components_.mem,
                             *components_.pla,
                             *components_.sid,
                             *components_.uiBridge,
                             *components_.vic,
                             cpuCfg_,
                             pendingBusPrime,
                             busPrimedAfterBoot,
                             BASIC_ROM,
                             KERNAL_ROM,
                             CHAR_ROM,
                             running,
                             uiQuit,
                             uiPaused);

    return session.run();
}

void Computer::warmReset()
{
     if (components_.resetCtl) components_.resetCtl->warmReset();
}

void Computer::coldReset()
{
     if (components_.resetCtl) components_.resetCtl->coldReset();
}

void Computer::setVideoMode(const std::string& mode)
{
    if (components_.resetCtl) components_.resetCtl->setVideoMode(mode);
}

void Computer::wireUp()
{
    // Attach components to each other
    components_.debug = std::make_unique<DebugManager>(uiPaused);

    components_.debug->wireBackend(this, components_.cart.get(), components_.cass.get(), components_.cia1.get(), components_.cia2.get(),
                                   components_.cpu.get(), components_.bus.get(), components_.io.get(), components_.keyb.get(),
                                   components_.logger.get(), components_.mem.get(), components_.pla.get(), components_.sid.get(), components_.vic.get());

    components_.debug->wireTrace(components_.cart.get(), components_.cia1.get(), components_.cia2.get(), components_.cpu.get(),
                                 components_.mem.get(), components_.pla.get(), components_.sid.get(), components_.vic.get());

    components_.bus->attachCIA2Instance(components_.cia2.get());
    components_.bus->attachLogInstance(components_.logger.get());

    components_.cart->attachCPUInstance(components_.cpu.get());
    components_.cart->attachHostInstance(this);
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
    components_.io->setMonitorOpenCallback([this]() -> bool { return this->components_.debug && this->components_.debug->monitorController().isOpen();});

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

    components_.media = std::make_unique<MediaManager>(components_.cart, components_.drives, this, *components_.bus, *components_.mem, *components_.pla,
                                                       *components_.cpu, *components_.vic, components_.debug->backend(), components_.debug->trace(),
                                                       *components_.cass, *components_.logger, D1541LoROM, D1541HiROM, D1571ROM, D1581ROM,
                                                       [this]() { if (!busPrimedAfterBoot) pendingBusPrime = true; }, [this]() { this->coldReset(); });

    if (components_.media) components_.media->setVideoMode(videoMode_);

    components_.inputRouter = std::make_unique<InputRouter>(uiPaused, &components_.debug->monitorController(), components_.inputMgr.get(),
                                                           components_.media.get(), [this]() { warmReset(); }, [this]() { coldReset(); },
                                                           [this]() { if (components_.uiBridge) components_.uiBridge->toggleManualPause(); });

    components_.resetCtl = std::make_unique<ResetController>(*components_.cpu, *components_.mem, *components_.pla, *components_.cia1,
                                                             *components_.cia2, *components_.vic, *components_.sid, *components_.bus,
                                                             *components_.cart, components_.media.get(), BASIC_ROM, KERNAL_ROM, CHAR_ROM, videoMode_,
                                                             cpuCfg_);

    components_.uiBridge = std::make_unique<UIBridge>(*components_.ui, components_.media.get(), components_.inputMgr.get(), uiPaused, running,
                                                      [this](const std::string& p) { this->saveStateToFile(p); },
    [this](const std::string& p) { this->loadStateFromFile(p); }, [this]() { this->warmReset(); }, [this]() { this->coldReset(); },
    [this](const std::string& mode) { this->setVideoMode(mode); }, [this]() { this->enterMonitor(); },
    [this]() -> bool { return this->videoMode_ == VideoMode::PAL; },
    [this]() -> bool { return this->components_.debug && this->components_.debug->monitorController().isOpen(); });

    components_.stateMgr = std::make_unique<StateManager>(*components_.cart, *components_.cass, *components_.cia1, *components_.cia2,
                                                          *components_.cpu, *components_.bus, *components_.inputMgr, *components_.logger,
                                                          *components_.media, *components_.mem, *components_.pla, *components_.sid, *components_.vic,
                                                          uiPaused, videoMode_, cpuCfg_, pendingBusPrime, busPrimedAfterBoot, components_.drives);
}
