// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "DebugManager.h"
#include "Computer.h"
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
    // Initialize writer
    StateWriter wrtr(kStateVersion);
    wrtr.beginFile();

    // SYS0 = Core system config
    wrtr.beginChunk("SYS0");

    // Dump SYS0 schema version
    wrtr.writeU32(1);

    // Dump Video mode
    wrtr.writeU8(static_cast<uint8_t>(videoMode_));

    // Dump CPU timing ID
    const uint8_t cpuTimingId = (videoMode_ == VideoMode::NTSC) ? 0 : 1;
    wrtr.writeU8(cpuTimingId);

    // Dump UI pause state
    wrtr.writeBool(uiPaused.load());

    // Dump Bus priming flags
    wrtr.writeBool(pendingBusPrime);
    wrtr.writeBool(busPrimedAfterBoot);

    // Dump Drive config
    wrtr.writeU8(16);
    for (int i = 0; i < 16; ++i)
    {
        const bool present = (drives[i] != nullptr);
        wrtr.writeBool(present);

        if (present)
        {
            wrtr.writeU8(static_cast<uint8_t>(drives[i]->getDriveModel()));
            wrtr.writeU8(static_cast<uint8_t>(drives[i]->getDeviceNumber()));
        }
    }

    wrtr.endChunk(); // end SYS0

    // -------------------------
    // Device chunks (next)
    // -------------------------
    if (processor)  processor->saveState(wrtr);
    if (cia1object) cia1object->saveState(wrtr);
    if (cia2object) cia2object->saveState(wrtr);
    if (vicII)      vicII->saveState(wrtr);
    if (sidchip)    sidchip->saveState(wrtr);
    if (pla)        pla->saveState(wrtr);
    if (mem)        mem->saveState(wrtr);
    if (bus)        bus->saveState(wrtr);

    // Save joystick state
    if (inputMgr)   inputMgr->saveState(wrtr);

    // Save media state
    if (media)      media->saveState(wrtr);

    // Save cartridge state if attached
    if (media && cart && media->getState().cartAttached) cart->saveState(wrtr);

    // Save Cassette and tape state if attached
    if (media && cass && media->getState().tapeAttached) cass->saveState(wrtr);

    // Write file
    return wrtr.writeToFile(path);
}

bool Computer::loadStateFromFile(const std::string& path)
{
    StateReader rdr;

    // Try to read given file
    bool loaded = rdr.loadFromFile(path);
    bool validate = rdr.readFileHeader();

    // Fail if we can't load or validate the file
    if (!loaded || !validate)
    {
        #ifdef Debug
        std::cout << "Unable to load .sav file!\n";
        #endif // Debug
        return false;
    }

    // Process the first chunk
    StateReader::Chunk chunk;
    if (!rdr.nextChunk(chunk)) return false;

    if (std::memcmp(chunk.tag, "SYS0", 4) == 0)
    {
        rdr.enterChunkPayload(chunk);

        uint32_t sysVer = 0;
        if (!rdr.readU32(sysVer)) return false;
        if (sysVer != 1) return false;

        // Restore Video Mode
        uint8_t mode = 0;
        if (!rdr.readU8(mode)) return false;
        videoMode_ = static_cast<VideoMode>(mode);

        // Restore CPU timing
        uint8_t cpuTimingID = 0;
        if (!rdr.readU8(cpuTimingID)) return false;
        cpuCfg_ = cpuTimingID ? &PAL_CPU : &NTSC_CPU;

        // Restore uiPaused
        bool tmpPaused = false;
        if (!rdr.readBool(tmpPaused)) return false;
        uiPaused.store(tmpPaused);

        // Restore bus pending status
        if (!rdr.readBool(pendingBusPrime)) return false;
        if (!rdr.readBool(busPrimedAfterBoot)) return false;

        // End the chunk
        rdr.exitChunkPayload(chunk);

        while (rdr.nextChunk(chunk))
        {
            const bool isCPU = (std::memcmp(chunk.tag, "CPU0", 4) == 0) ||
                               (std::memcmp(chunk.tag, "CPUX", 4) == 0);

            const bool isCIA1 = (std::memcmp(chunk.tag, "CIA1", 4) == 0) ||
                                (std::memcmp(chunk.tag, "CI1X", 4) == 0);

            const bool isCIA2 = (std::memcmp(chunk.tag, "CIA2", 4) == 0) ||
                                (std::memcmp(chunk.tag, "CI2X", 4) == 0);

            const bool isVIC = (std::memcmp(chunk.tag, "VIC0", 4) == 0) ||
                               (std::memcmp(chunk.tag, "VICX", 4) == 0);

            const bool isSID = (std::memcmp(chunk.tag, "SID0", 4) == 0) ||
                               (std::memcmp(chunk.tag, "SIDX", 4) == 0);
            #ifdef Debug
            std::cout << "CHUNK: "
                      << char(chunk.tag[0]) << char(chunk.tag[1]) << char(chunk.tag[2]) << char(chunk.tag[3])
                      << "\n";
            #endif

            if (isCPU)
            {
                if (!(processor && processor->loadState(chunk, rdr))) return false;
                #ifdef Debug
                std::cout << "Loaded processor\n";
                #endif
            }
            else if (std::memcmp(chunk.tag, "MEM0", 4) == 0)
            {
                if (!(mem && mem->loadState(chunk, rdr))) return false;
                #ifdef Debug
                std::cout << "Loaded memory\n";
                #endif
            }
            else if (isCIA1)
            {
                if (!(cia1object && cia1object->loadState(chunk, rdr))) return false;
                #ifdef Debug
                std::cout << "Loaded CIA1\n";
                #endif
            }
            else if (isCIA2)
            {
                if (!(cia2object && cia2object->loadState(chunk, rdr))) return false;
                #ifdef Debug
                std::cout << "Loaded CIA2\n";
                #endif
            }
            else if (isVIC)
            {
                if (!(vicII && vicII->loadState(chunk, rdr))) return false;
                #ifdef Debug
                std::cout << "Loaded VIC\n";
                #endif
            }
            else if (isSID)
            {
                if (!(sidchip && sidchip->loadState(chunk, rdr))) return false;
                #ifdef Debug
                std::cout << "Loaded SID\n";
                #endif
            }
            else if (std::memcmp(chunk.tag, "PLA0", 4) == 0)
            {
                if (!(pla && pla->loadState(chunk, rdr))) return false;
                #ifdef Debug
                std::cout << "Loaded PLA\n";
                #endif
            }
            else if (std::memcmp(chunk.tag, "MED0", 4) == 0)
            {
                if (!(media && media->loadState(chunk, rdr))) return false;
                #ifdef Debug
                std::cout << "Loaded Media Manager\n";
                #endif
            }
            else if (std::memcmp(chunk.tag, "CART", 4) == 0)
            {
                if (media && media->isCartridgeAttached())
                    media->restoreCartridgeFromState();
                if (!(cart && cart->loadState(chunk, rdr))) return false;
                #ifdef Debug
                std::cout << "Loaded Cartridge\n";
                #endif
            }
            else if (std::memcmp(chunk.tag, "CASS", 4) == 0)
            {
                if (!(cass && cass->loadState(chunk, rdr))) return false;
                #ifdef Debug
                std::cout << "Loaded Cassette\n";
                #endif
            }
            else
            {
                rdr.skipChunk(chunk);
            }
        }

        // Success
        return true;
    }

    // Failure
    return false;
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

        if (debug) debug->tick();
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
                    if (!uiPaused.load() && debug && debug->hasBreakpoint(pc))
                    {
                        uiPaused = true;
                        debug->onBreakpoint(pc);
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
    debug = std::make_unique<DebugManager>(uiPaused);

    debug->wireBackend(this, cart.get(), cass.get(), cia1object.get(), cia2object.get(), processor.get(), bus.get(), IO_adapter.get(), keyb.get(),
                        logger.get(), mem.get(), pla.get(), sidchip.get(), vicII.get());

    debug->wireTrace(cart.get(), cia1object.get(), cia2object.get(), processor.get(), mem.get(), pla.get(), sidchip.get(), vicII.get());

    bus->attachCIA2Instance(cia2object.get());
    bus->attachLogInstance(logger.get());

    cart->attachCPUInstance(processor.get());
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

    media = std::make_unique<MediaManager>(cart, drives, *bus, *mem, *pla, *processor, *vicII, debug->backend(), debug->trace(), *cass, *logger,
    D1541LoROM, D1541HiROM, D1571ROM, D1581ROM, [this]() { if (!busPrimedAfterBoot) pendingBusPrime = true; }, [this]() { this->coldReset(); });

    if (media) media->setVideoMode(videoMode_);

    inputRouter = std::make_unique<InputRouter>(uiPaused, &debug->monitorController(), inputMgr.get(), media.get(), [this]() { warmReset(); },
    [this]() { coldReset(); });

    resetCtl = std::make_unique<ResetController>(*processor, *mem, *pla, *cia1object, *cia2object, *vicII, *sidchip, *bus,
    *cart, media.get(), BASIC_ROM, KERNAL_ROM, CHAR_ROM, videoMode_, cpuCfg_);

    uiBridge = std::make_unique<UIBridge>(*ui, media.get(), inputMgr.get(), uiPaused, running, [this](const std::string& path) { saveStateToFile(path); },
    [this](const std::string& path) { loadStateFromFile(path); }, [this]() { warmReset(); }, [this]() { coldReset(); },
    [this](const std::string& mode) { setVideoMode(mode); }, [this]() { enterMonitor(); }, [this]() { return videoMode_ == VideoMode::PAL; });
}
