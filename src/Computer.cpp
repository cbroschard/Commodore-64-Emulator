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
    cart(std::make_unique<Cartridge>()),
    cass(std::make_unique<Cassette>()),
    cia1object(std::make_unique<CIA1>()),
    cia2object(std::make_unique<CIA2>()),
    processor(std::make_unique<CPU>()),
    ui(std::make_unique<EmulatorUI>()),
    bus(std::make_unique<IECBUS>()),
    inputMgr(std::make_unique<InputManager>()),
    IRQ(std::make_unique<IRQLine>()),
    keyb(std::make_unique<Keyboard>()),
    logger(std::make_unique<Logging>("debug.txt")),
    mem(std::make_unique<Memory>()),
    pla(std::make_unique<PLA>()),
    sidchip(std::make_unique<SID>(44100)),
    IO_adapter(std::make_unique<IO>()),
    vicII(std::make_unique<Vic>()),
    videoMode_(VideoMode::NTSC),
    cpuCfg_(&NTSC_CPU),
    running(true),
    uiQuit(false),
    uiPaused(false),
    pendingBusPrime(false),
    busPrimedAfterBoot(false)
{
    // Wire components
    wireUp();
}

Computer::~Computer() noexcept
{
    try
    {
        if (IO_adapter)
        {
            // Ensure the render thread is down
            if (debug) debug->closeMonitor();
            running = false;
            IO_adapter->stopRenderThread(running);
            IO_adapter->setGuiCallback({});
            IO_adapter->setInputCallback({});

            // Audio shutdown
            IO_adapter->stopAudio();
        }

        if (logger) logger->flush();
    }
    catch(...)
    {

    }
}

bool Computer::saveStateToFile(const std::string& path)
{
    return stateMgr ? stateMgr->save(path) : false;
}

bool Computer::loadStateFromFile(const std::string& path)
{
    return stateMgr ? stateMgr->load(path) : false;
}

void Computer::requestColdReset()
{
    if (resetCtl)
        resetCtl->coldReset();
}

void Computer::requestWarmReset()
{
    if (resetCtl)
        resetCtl->warmReset();
}

void Computer::requestCartridgeNMI()
{
    if (processor)
        processor->pulseNMI();
}

void Computer::setJoystickAttached(int port, bool flag)
{
    if (inputMgr) inputMgr->setJoystickAttached(port, flag);
}

void Computer::set1541LoROM(const std::string& loROM)
{
    D1541LoROM = loROM;
    if (media) media->setD1541LoROM(loROM);
}

void Computer::set1541HiROM(const std::string& hiROM)
{
    D1541HiROM = hiROM;
    if (media) media->setD1541HiROM(hiROM);
}

void Computer::set1571ROM(const std::string& rom)
{
    D1571ROM = rom;
    if (media) media->setD1571ROM(rom);
}

void Computer::set1581ROM(const std::string& rom)
{
    D1581ROM = rom;
    if (media) media->setD1581ROM(rom);
}

void Computer::enterMonitor()
{
    if (debug) debug->openMonitor();
}

void Computer::setJoystickConfig(int port, JoystickMapping& cfg)
{
    if (!inputMgr) return;
    inputMgr->setJoystickConfig(port, cfg);
}

bool Computer::boot()
{
    EmulationSession session(*cart,
                             *cia1object,
                             *cia2object,
                             *processor,
                             *debug,
                             *ui,
                             *bus,
                             *inputMgr,
                             *inputRouter,
                             *IO_adapter,
                             *logger,
                             *media,
                             *mem,
                             *pla,
                             *sidchip,
                             *uiBridge,
                             *vicII,
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
     if (resetCtl) resetCtl->warmReset();
}

void Computer::coldReset()
{
     if (resetCtl) resetCtl->coldReset();
}

void Computer::setVideoMode(const std::string& mode)
{
    if (resetCtl) resetCtl->setVideoMode(mode);
}

void Computer::wireUp()
{
    // Attach components to each other
    debug = std::make_unique<DebugManager>(uiPaused);

    debug->wireBackend(this, cart.get(), cass.get(), cia1object.get(), cia2object.get(), processor.get(), bus.get(), IO_adapter.get(), keyb.get(),
                        logger.get(), mem.get(), pla.get(), sidchip.get(), vicII.get());

    debug->wireTrace(cart.get(), cia1object.get(), cia2object.get(), processor.get(), mem.get(), pla.get(), sidchip.get(), vicII.get());

    bus->attachCIA2Instance(cia2object.get());
    bus->attachLogInstance(logger.get());

    cart->attachCPUInstance(processor.get());
    cart->attachHostInstance(this);
    cart->attachMemoryInstance(mem.get());
    cart->attachLogInstance(logger.get());
    cart->attachTraceManagerInstance(&debug->trace());
    cart->attachVicInstance(vicII.get());

    cass->attachMemoryInstance(mem.get());
    cass->attachLogInstance(logger.get());

    cia1object->attachCassetteInstance(cass.get());
    cia1object->attachCPUInstance(processor.get());
    cia1object->attachIRQLineInstance(IRQ.get());
    cia1object->attachKeyboardInstance(keyb.get());
    cia1object->attachLogInstance(logger.get());
    cia1object->attachMemoryInstance(mem.get());
    cia1object->attachTraceManagerInstance(&debug->trace());
    cia1object->attachVicInstance(vicII.get());

    cia2object->attachCPUInstance(processor.get());
    cia2object->attachIECBusInstance(bus.get());
    cia2object->attachLogInstance(logger.get());
    cia2object->attachTraceManagerInstance(&debug->trace());
    cia2object->attachVicInstance(vicII.get());

    IO_adapter->attachVICInstance(vicII.get());
    IO_adapter->attachSIDInstance(sidchip.get());
    IO_adapter->attachLogInstance(logger.get());

    IO_adapter->setMonitorOpenCallback([this]() -> bool { return this->debug && this->debug->monitorController().isOpen();});

    keyb->attachLogInstance(logger.get());

    mem->attachProcessorInstance(processor.get());
    mem->attachVICInstance(vicII.get());
    mem->attachCIA1Instance(cia1object.get());
    mem->attachCIA2Instance(cia2object.get());
    mem->attachSIDInstance(sidchip.get());
    mem->attachCartridgeInstance(cart.get());
    mem->attachCassetteInstance(cass.get());
    mem->attachPLAInstance(pla.get());
    mem->attachMonitorInstance(&debug->monitor());;
    mem->attachLogInstance(logger.get());
    mem->attachTraceManagerInstance(&debug->trace());

    inputMgr->attachCIA1Instance(cia1object.get());
    inputMgr->attachKeyboardInstance(keyb.get());
    inputMgr->attachMonitorControllerInstance(&debug->monitorController());

    pla->attachCartridgeInstance(cart.get());
    pla->attachCPUInstance(processor.get());
    pla->attachLogInstance(logger.get());
    pla->attachTraceManagerInstance(&debug->trace());
    pla->attachVICInstance(vicII.get());

    processor->attachMemoryInstance(mem.get());
    processor->attachCIA2Instance(cia2object.get());
    processor->attachVICInstance(vicII.get());
    processor->attachIRQLineInstance(IRQ.get());
    processor->attachLogInstance(logger.get());
    processor->attachTraceManagerInstance(&debug->trace());

    sidchip->attachCPUInstance(processor.get());;
    sidchip->attachLogInstance(logger.get());
    sidchip->attachTraceManagerInstance(&debug->trace());
    sidchip->attachVicInstance(vicII.get());

    vicII->attachIOInstance(IO_adapter.get());
    vicII->attachCPUInstance(processor.get());
    vicII->attachMemoryInstance(mem.get());
    vicII->attachCIA2Instance(cia2object.get());
    vicII->attachIRQLineInstance(IRQ.get());
    vicII->attachLogInstance(logger.get());
    vicII->attachTraceManagerInstance(&debug->trace());

    media = std::make_unique<MediaManager>(cart, drives, this, *bus, *mem, *pla, *processor, *vicII, debug->backend(), debug->trace(), *cass, *logger,
    D1541LoROM, D1541HiROM, D1571ROM, D1581ROM, [this]() { if (!busPrimedAfterBoot) pendingBusPrime = true; }, [this]() { this->coldReset(); });

    if (media) media->setVideoMode(videoMode_);

    inputRouter = std::make_unique<InputRouter>(uiPaused, &debug->monitorController(), inputMgr.get(), media.get(), [this]() { warmReset(); },
    [this]() { coldReset(); }, [this]() { if (uiBridge) uiBridge->toggleManualPause(); });

    resetCtl = std::make_unique<ResetController>(*processor, *mem, *pla, *cia1object, *cia2object, *vicII, *sidchip, *bus,
    *cart, media.get(), BASIC_ROM, KERNAL_ROM, CHAR_ROM, videoMode_, cpuCfg_);

    uiBridge = std::make_unique<UIBridge>(*ui, media.get(), inputMgr.get(), uiPaused, running, [this](const std::string& p) { this->saveStateToFile(p); },
    [this](const std::string& p) { this->loadStateFromFile(p); }, [this]() { this->warmReset(); }, [this]() { this->coldReset(); },
    [this](const std::string& mode) { this->setVideoMode(mode); }, [this]() { this->enterMonitor(); },
    [this]() -> bool { return this->videoMode_ == VideoMode::PAL; }, [this]() -> bool { return this->debug && this->debug->monitorController().isOpen(); });

    stateMgr = std::make_unique<StateManager>(*cart, *cass, *cia1object, *cia2object, *processor, *bus, *inputMgr, *logger, *media, *mem, *pla, *sidchip,
    *vicII, uiPaused, videoMode_, cpuCfg_, pendingBusPrime, busPrimedAfterBoot, drives);
}
