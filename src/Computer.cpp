// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Debug/MLMonitor.h"
#include "computer.h"
#include "ResetController.h"
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
    monitor(std::make_unique<MLMonitor>()),
    monbackend(std::make_unique<MLMonitorBackend>()),
    pla(std::make_unique<PLA>()),
    sidchip(std::make_unique<SID>(44100)),
    IO_adapter(std::make_unique<IO>()),
    traceMgr(std::make_unique<TraceManager>()),
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
            if (monitorCtl) monitorCtl->close();
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
    if (monitorCtl) monitorCtl->open();
}

bool Computer::handleInputEvent(const SDL_Event& ev)
{
    // F12 global monitor toggle (do this BEFORE monitorCtl->handleEvent)
    if (ev.type == SDL_KEYDOWN && !ev.key.repeat &&
        ev.key.keysym.scancode == SDL_SCANCODE_F12)
    {
        if (monitorCtl) monitorCtl->toggle();
        return true;
    }

    // Let monitor consume events next
    if (monitorCtl && monitorCtl->handleEvent(ev)) return true;

    if (ev.type == SDL_KEYDOWN && !ev.key.repeat)
    {
        const SDL_Scancode sc = ev.key.keysym.scancode;
        const SDL_Keymod mods = static_cast<SDL_Keymod>(ev.key.keysym.mod);

        // SPACE pause
        if (sc == SDL_SCANCODE_SPACE)
        {
            uiPaused = !uiPaused.load();
            return true;
        }

        // CTRL+W warm reset
        if ((mods & KMOD_CTRL) && sc == SDL_SCANCODE_W)
        {
            warmReset();
            return true;
        }

        // CTRL+SHIFT+R cold reset
        if ((mods & KMOD_CTRL) && (mods & KMOD_SHIFT) && sc == SDL_SCANCODE_R)
        {
            coldReset();
            return true;
        }

        // ALT cassette controls
        if (mods & KMOD_ALT)
        {
            if (sc == SDL_SCANCODE_P) { if (media) media->tapePlay();   return true; }
            if (sc == SDL_SCANCODE_S) { if (media) media->tapeStop();   return true; }
            if (sc == SDL_SCANCODE_R) { if (media) media->tapeRewind(); return true; }
            if (sc == SDL_SCANCODE_E) { if (media) media->tapeEject();  return true; }
        }
    }

    // Feed InputManager (keyboard/joystick mapping)
    if (inputMgr) return inputMgr->handleEvent(ev);
    return false;
}

void Computer::setJoystickConfig(int port, JoystickMapping& cfg)
{
    if (!inputMgr) return;
    inputMgr->setJoystickConfig(port, cfg);
}

bool Computer::boot()
{
    if (!mem->Initialize(BASIC_ROM, KERNAL_ROM, CHAR_ROM))
    {
        throw std::runtime_error("Error: Problem encountered initializing memory!");
    }

    // Reset all chips
    bus->reset();
    pla->reset();
    processor->reset();
    vicII->reset();
    cia1object->reset();
    cia2object->reset();
    sidchip->reset();

    // Attach backend pointer to MLMonitor
    monitor->attachMLMonitorBackendInstance(monbackend.get());

    // Attach all devices to monitor back end
    monbackend->attachCartridgeInstance(cart.get());
    monbackend->attachCassetteInstance(cass.get());
    monbackend->attachCIA1Instance(cia1object.get());
    monbackend->attachCIA2Instance(cia2object.get());
    monbackend->attachComputerInstance(this);
    monbackend->attachProcessorInstance(processor.get());
    monbackend->attachIECBusInstance(bus.get());
    monbackend->attachIOInstance(IO_adapter.get());
    monbackend->attachKeyboardInstance(keyb.get());
    monbackend->attachLogInstance(logger.get());
    monbackend->attachMemoryInstance(mem.get());
    monbackend->attachPLAInstance(pla.get());
    monbackend->attachSIDInstance(sidchip.get());
    monbackend->attachVICInstance(vicII.get());

    // Wire up the trace manager back end as well
    traceMgr->attachCartInstance(cart.get());
    traceMgr->attachCIA1Instance(cia1object.get());
    traceMgr->attachCIA2Instance(cia2object.get());
    traceMgr->attachCPUInstance(processor.get());
    traceMgr->attachMemoryInstance(mem.get());
    traceMgr->attachPLAInstance(pla.get());
    traceMgr->attachSIDInstance(sidchip.get());
    traceMgr->attachVicInstance(vicII.get());

    // Attach the trace manager to the trace command now that we're all wired up
    monitor->attachTraceManagerInstance(traceMgr.get());

    // If any command line attachments were passed in, process them
    if (media) media->applyBootAttachments();

    // **Start Audio Playback**
    IO_adapter->playAudio();
    sidchip->setSampleRate(IO_adapter->getSampleRate());

    // show the ImGui menu
    IO_adapter->setGuiCallback([this]()
    {
        if (ui) ui->draw();
    });

    // Graphics rendering thread
    IO_adapter->startRenderThread(running);
    IO_adapter->finishFrameAndSignal();

    const auto frameDuration = std::chrono::duration<double, std::milli>(1000.0 / cpuCfg_->frameRate);
    auto nextFrameTime = std::chrono::steady_clock::now() + frameDuration;

    while (true)
    {
        SDL_Event e;
        while (SDL_PollEvent(&e))
        {
            // InputRouter handles hotkeys + monitor + controller hotplug + input mapping
            if (inputRouter && inputRouter->handleEvent(e))
                continue;

            // Forward the event to ImGui/render thread
            IO_adapter->enqueueEvent(e);

            // Quit
            if (e.type == SDL_QUIT)
            {
                running = false;
                uiQuit = true;
            }
        }

        if (monitorCtl) monitorCtl->tick();
        if (inputMgr) inputMgr->tick();

        if (pendingBusPrime)
        {
            // Ok to reset the IEC Bus
            bus->reset();
            pendingBusPrime     = false;
            busPrimedAfterBoot  = true;
        }

        int frameCycles = 0;

        // Main frame loop
        while (frameCycles < cpuCfg_->cyclesPerFrame())
        {
            uint32_t elapsedCycles = 0;

            if (vicII->getAEC())
            {
                // CPU executes an instruction when AEC is high
                try
                {
                    // Check for breakpoint
                    uint16_t pc = processor->getPC();
                    if (monitor && monitor->hasBreakpoint(pc))
                    {
                        // Pause the emulator so state is stable while you're in the monitor
                        uiPaused = true;

                        // Push the notification into the monitor window (same mechanism as watchpoints)
                        char msg[64];
                        std::snprintf(msg, sizeof(msg), ">>> Breakpoint hit at $%04X", pc);
                        monitor->queueAsyncLine(msg);

                        // Bring up the monitor window (or focus it if already open)
                        if (monitorCtl) monitorCtl->open();

                        // Stop executing immediately
                        break;
                    }

                    // Execute one CPU instruction
                    processor->tick();
                    elapsedCycles = processor->getElapsedCycles();

                    // If a watch point paused us during the instruction, stop immediately.
                    if (uiPaused.load()) break;
                }
                catch (const std::exception& e)
                {
                    std::cerr << "Exception caught: " << e.what() << "\n";
                    logger->flush();
                    return false;
                }
            }
            else
            {
                // AEC is low: CPU is idle, but VIC-II processes raster updates
                elapsedCycles = 1;
            }

            // Step the SID
            sidchip->tick(elapsedCycles);

            // Synchronize CIA timers
            cia1object->updateTimers(elapsedCycles);
            cia2object->updateTimers(elapsedCycles);

            // VIC-II raster updates always occur
            vicII->tick(elapsedCycles);

            for (int dev = 8; dev <= 11; ++dev)
            {
                if (drives[dev]) drives[dev]->tick(elapsedCycles);
            }

            bus->tick(elapsedCycles);

            if (vicII->isFrameDone())
            {
                vicII->clearFrameFlag();
                IO_adapter->finishFrameAndSignal();   // hand frame to render thread
            }

            // Add elapsed cycles to the frame count
            frameCycles += elapsedCycles;
        }

        // Process menu items
        if (uiQuit.exchange(false))
        {
            running = false;
            break;
        }

        if (ui && uiBridge) ui->setMediaViewState(uiBridge->buildMediaViewState());

        // Enforce 60Hz frame timing by sleeping until the next frame target time.
        auto now = std::chrono::steady_clock::now();
        if (now < nextFrameTime)
        {
            std::this_thread::sleep_until(nextFrameTime);
            nextFrameTime += frameDuration; // Added to test
        }
        else
        {
            nextFrameTime = now + frameDuration; // Added to test
        }
        if (uiBridge) uiBridge->processCommands();
        if (media) media->tick();
        if (!running) break;
    }

    if (IO_adapter)
    {
        running = false;
        IO_adapter->stopRenderThread(running);
        IO_adapter->setGuiCallback({});
        IO_adapter->setInputCallback({});
    }

    return true;
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
    bus->attachCIA2Instance(cia2object.get());
    bus->attachLogInstance(logger.get());

    cart->attachCPUInstance(processor.get());
    cart->attachMemoryInstance(mem.get());
    cart->attachLogInstance(logger.get());
    cart->attachTraceManagerInstance(traceMgr.get());
    cart->attachVicInstance(vicII.get());

    cass->attachMemoryInstance(mem.get());
    cass->attachLogInstance(logger.get());

    cia1object->attachCassetteInstance(cass.get());
    cia1object->attachCPUInstance(processor.get());
    cia1object->attachIRQLineInstance(IRQ.get());
    cia1object->attachKeyboardInstance(keyb.get());
    cia1object->attachLogInstance(logger.get());
    cia1object->attachMemoryInstance(mem.get());
    cia1object->attachTraceManagerInstance(traceMgr.get());
    cia1object->attachVicInstance(vicII.get());

    cia2object->attachCPUInstance(processor.get());
    cia2object->attachIECBusInstance(bus.get());
    cia2object->attachLogInstance(logger.get());
    cia2object->attachTraceManagerInstance(traceMgr.get());
    cia2object->attachVicInstance(vicII.get());

    IO_adapter->attachVICInstance(vicII.get());
    IO_adapter->attachSIDInstance(sidchip.get());
    IO_adapter->attachLogInstance(logger.get());

    keyb->attachLogInstance(logger.get());

    mem->attachProcessorInstance(processor.get());
    mem->attachVICInstance(vicII.get());
    mem->attachCIA1Instance(cia1object.get());
    mem->attachCIA2Instance(cia2object.get());
    mem->attachSIDInstance(sidchip.get());
    mem->attachCartridgeInstance(cart.get());
    mem->attachCassetteInstance(cass.get());
    mem->attachPLAInstance(pla.get());
    mem->attachMonitorInstance(monitor.get());;
    mem->attachLogInstance(logger.get());
    mem->attachTraceManagerInstance(traceMgr.get());

    monitorCtl = std::make_unique<MonitorController>(uiPaused);
    monitorCtl->attachMonitorInstance(monitor.get());

    inputMgr->attachCIA1Instance(cia1object.get());
    inputMgr->attachKeyboardInstance(keyb.get());
    inputMgr->attachMonitorControllerInstance(monitorCtl.get());

    pla->attachCartridgeInstance(cart.get());
    pla->attachCPUInstance(processor.get());
    pla->attachLogInstance(logger.get());
    pla->attachTraceManagerInstance(traceMgr.get());
    pla->attachVICInstance(vicII.get());

    processor->attachMemoryInstance(mem.get());
    processor->attachCIA2Instance(cia2object.get());
    processor->attachVICInstance(vicII.get());
    processor->attachIRQLineInstance(IRQ.get());
    processor->attachLogInstance(logger.get());
    processor->attachTraceManagerInstance(traceMgr.get());

    sidchip->attachCPUInstance(processor.get());;
    sidchip->attachLogInstance(logger.get());
    sidchip->attachTraceManagerInstance(traceMgr.get());
    sidchip->attachVicInstance(vicII.get());

    vicII->attachIOInstance(IO_adapter.get());
    vicII->attachCPUInstance(processor.get());
    vicII->attachMemoryInstance(mem.get());
    vicII->attachCIA2Instance(cia2object.get());
    vicII->attachIRQLineInstance(IRQ.get());
    vicII->attachLogInstance(logger.get());
    vicII->attachTraceManagerInstance(traceMgr.get());

    media = std::make_unique<MediaManager>(cart, drives, *bus, *mem, *pla, *processor, *vicII, *monbackend, *traceMgr, *cass, *logger,
                                            D1541LoROM, D1541HiROM, D1571ROM, D1581ROM,
                                            [this]() { if (!busPrimedAfterBoot) pendingBusPrime = true; },
                                            [this]() { this->coldReset(); });

    if (media) media->setVideoMode(videoMode_);

    inputRouter = std::make_unique<InputRouter>(uiPaused, monitorCtl.get(), inputMgr.get(), media.get(), [this]() { warmReset(); },
                                   [this]() { coldReset(); });

    resetCtl = std::make_unique<ResetController>(*processor, *mem, *pla, *cia1object, *cia2object, *vicII, *sidchip, *bus,
                                *cart, media.get(), BASIC_ROM, KERNAL_ROM, CHAR_ROM, videoMode_, cpuCfg_);

    uiBridge = std::make_unique<UIBridge>(*ui, media.get(), inputMgr.get(), uiPaused, running, [this]() { warmReset(); },
    [this]() { coldReset(); }, [this](const std::string& mode) { setVideoMode(mode); }, [this]() { enterMonitor(); },
    [this]() { return videoMode_ == VideoMode::PAL; });
}
