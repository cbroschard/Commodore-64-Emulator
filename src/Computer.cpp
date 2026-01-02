// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Debug/MLMonitor.h"
#include "computer.h"

Computer::Computer() :
    cart(std::make_unique<Cartridge>()),
    cass(std::make_unique<Cassette>()),
    cia1object(std::make_unique<CIA1>()),
    cia2object(std::make_unique<CIA2>()),
    processor(std::make_unique<CPU>()),
    bus(std::make_unique<IECBUS>()),
    input(std::make_unique<InputManager>()),
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
    prgDelay(140),
    videoMode_(VideoMode::NTSC),
    cpuCfg_(&NTSC_CPU),
    cartridgeAttached(false),
    cartridgePath(""),
    tapeAttached(false),
    tapePath(""),
    prgAttached(false),
    prgLoaded(false),
    prgPath(""),
    diskAttached(false),
    diskPath(""),
    frameReady(false),
    running(true),
    uiVideoModeReq(-1),
    uiAttachD64(false),
    uiAttachPRG(false),
    uiAttachCRT(false),
    uiAttachT64(false),
    uiAttachTAP(false),
    uiToggleJoy1Req(false),
    uiToggleJoy2Req(false),
    uiQuit(false),
    uiWarmReset(false),
    uiColdReset(false),
    uiPaused(false),
    uiEnterMonitor(false)
{
    // Initialize file dialog struct
    fileDlg.open = false;
    fileDlg.currentDir = std::filesystem::current_path();

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

    input->attachCIA1Instance(cia1object.get());
    input->attachKeyboardInstance(keyb.get());
    input->attachMonitorControllerInstance(monitorCtl.get());

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
    if (input) input->setJoystickAttached(port, flag);
}

bool Computer::checkCombo(SDL_Keymod modMask, SDL_Scancode a, SDL_Scancode b)
{
    const Uint8* ks = SDL_GetKeyboardState(nullptr);
    return (SDL_GetModState() & modMask) && ks[a] && ks[b];
}

void Computer::setVideoMode(const std::string& mode)
{
    if (mode == "PAL" || mode == "pal")
    {
        videoMode_ = VideoMode::PAL;
        cpuCfg_ = &PAL_CPU;
    } else
    {
        videoMode_ = VideoMode::NTSC;
        cpuCfg_ = &NTSC_CPU;
    }
    // inform the CPU< VIC, SID, and CIA1 of the same mode
    processor->setMode(videoMode_);
    vicII->setMode(videoMode_);
    sidchip->setMode(videoMode_);
    cia1object->setMode(videoMode_);
    cia2object->setMode(videoMode_);
}

void Computer::enterMonitor()
{
    if (monitorCtl) monitorCtl->open();
}

bool Computer::handleInputEvent(const SDL_Event& ev)
{
    if (monitorCtl && monitorCtl->handleEvent(ev)) return true;

    if ((ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) && !ev.key.repeat)
    {
        const bool down = (ev.type == SDL_KEYDOWN);
        const auto sc   = ev.key.keysym.scancode;
        const auto mods = SDL_GetModState();

        if (down && (mods & KMOD_ALT))
        {
            if (sc == SDL_SCANCODE_E) { uiCass = CassCmd::Eject;  return true; }
            if (sc == SDL_SCANCODE_P) { uiCass = CassCmd::Play;   return true; }
            if (sc == SDL_SCANCODE_R) { uiCass = CassCmd::Rewind; return true; }
            if (sc == SDL_SCANCODE_S) { uiCass = CassCmd::Stop;   return true; }
        }
    }

    if (input) return input->handleEvent(ev);
    return false;
}

void Computer::setJoystickConfig(int port, JoystickMapping& cfg)
{
    if (!input) return;
    input->setJoystickConfig(port, cfg);
}

void Computer::warmReset()
{
    #ifdef Debug
    std::cout << "Performing warm reset...\n";
    #endif
    if (!cartridgeAttached && cart)
    {
        cart->setGameLine(true);
        cart->setExROMLine(true);
    }

    // Reset memory control register to default
    pla->updateMemoryControlRegister(0x37);

    // Reset major chips
    vicII->reset();
    cia1object->reset();
    cia2object->reset();
    sidchip->reset();

    // Reset CPU (reloads PC from $FFFC/$FFFD)
    processor->reset();
}

void Computer::coldReset()
{
    #ifdef Debug
        std::cout << "Performing cold reset.\n";
    #endif

    // If there's any drives attached, reset them as well
    if (drive8) drive8->reset();

    // If no cart attached, force default lines (KERNAL boot)
    if (!cartridgeAttached && cart)
    {
        cart->setGameLine(true);
        cart->setExROMLine(true);
    }

    // Power-cycle RAM/ROM init
    if (!mem->Initialize(BASIC_ROM, KERNAL_ROM, CHAR_ROM))
        throw std::runtime_error("Error: Problem encountered initializing memory!");

    // Reset PLA FIRST (because it likely clears "cart attached" bookkeeping)
    pla->reset();

    // Re-assert cartridge presence AFTER Initialize + PLA reset, BEFORE CPU reset
    if (cartridgeAttached)
    {
        mem->setCartridgeAttached(true);
        pla->setCartridgeAttached(true);
    }
    else
    {
        mem->setCartridgeAttached(false);
        pla->setCartridgeAttached(false);
    }

    // Reset major chips
    vicII->reset();
    cia1object->reset();
    cia2object->reset();
    sidchip->reset();

    // Reset CPU LAST so it fetches vectors under correct mapping
    processor->reset();
}

bool Computer::boot()
{
    if (!mem->Initialize(BASIC_ROM, KERNAL_ROM, CHAR_ROM))
    {
        throw std::runtime_error("Error: Problem encountered initializing memory!");
    }

    // Reset all chips
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

    // Check for a cartridge to be loaded, takes precedence over disk or tape
    if (cartridgeAttached)
    {
        // Update memory and PLA for bank switching
        mem->setCartridgeAttached(true);
        pla->setCartridgeAttached(true);


        if (!cart->loadROM(cartridgePath))
        {
            std::cout << "Unable to load cartridge: " << cartridgePath << "\n";
            // Load failed, exit completely
            return false;
        }
    }
    else if (tapeAttached)
    {
        if (!cass->loadCassette(tapePath, videoMode_))
        {
            std::cout << "Unable to load tape: " << tapePath << "\n";
        }
        if (cass->isT64())
        {
            T64LoadResult result = cass->t64LoadPrgIntoMemory();
            if (result.success)
            {
                // Scan for end of BASIC
                uint16_t scan = 0x0801;
                uint16_t nextLine;
                do
                {
                    nextLine = mem->read(scan) | (mem->read(scan + 1) << 8);
                    if (nextLine == 0) break;
                    scan = nextLine;
                } while (true);
                uint16_t basicEnd = scan + 2;

                // Update Pointers
                mem->writeDirect(0x2B, 0x01); mem->writeDirect(0x2C, 0x08);
                mem->writeDirect(0x2D, basicEnd & 0xFF); mem->writeDirect(0x2E, basicEnd >> 8);
                mem->writeDirect(0x2F, basicEnd & 0xFF); mem->writeDirect(0x30, basicEnd >> 8);
                mem->writeDirect(0x31, basicEnd & 0xFF); mem->writeDirect(0x32, basicEnd >> 8);

                // Inject RUN
                const uint8_t runKeys[4] = { 0x52, 0x55, 0x4E, 0x0D };
                mem->writeDirect(0xC6, 4);
                for (int i = 0; i < 4; ++i) mem->writeDirect(0x0277 + i, runKeys[i]);
            }
        }
    }
    else if (prgAttached)
    {
        if (!loadPrgImage())
        {
            std::cout << "Unable to load program: " << prgPath << "\n";
        }
        else
        {
            std::cout << "Successfully loaded the file: " << prgPath << "\n";
        }
    }
    else if (diskAttached)
    {
        attachD64Image();
    }

    // **Start Audio Playback**
    IO_adapter->playAudio();
    sidchip->setSampleRate(IO_adapter->getSampleRate());

    // Install the ImGui menu
    installMenu();

    IO_adapter->setInputCallback([this](const SDL_Event& ev)
    {
        if (ev.type == SDL_QUIT)
        {
            running = false;
            uiQuit  = true;
            return;
        }
        (void)this->handleInputEvent(ev);
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
            if (monitorCtl && monitorCtl->handleEvent(e)) continue;

            // F12 toggles the separate SDL monitor window (always, all builds)
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_F12)
            {
                uiEnterMonitor = true;
                continue;
            }

            if (e.type == SDL_CONTROLLERDEVICEADDED)
            {
                if (input) input->handleControllerDeviceAdded(e.cdevice.which);
                continue;
            }

            if (e.type == SDL_CONTROLLERDEVICEREMOVED)
            {
                if (input) input->handleControllerDeviceRemoved((SDL_JoystickID)e.cdevice.which);
                continue;
            }

            // forward to render thread for ImGui + input handling
            IO_adapter->enqueueEvent(e);
            if (e.type == SDL_QUIT)
            {
                running = false;
                uiQuit = true;
            }
        }

        if (monitorCtl) monitorCtl->tick();
        if (input) input->tick();

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

            if (drive8) drive8->tick(elapsedCycles);

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

        // File Menu
        if (uiAttachD64.exchange(false)) attachD64Image();
        if (uiAttachPRG.exchange(false)) attachPRGImage();
        if (uiAttachCRT.exchange(false)) attachCRTImage();
        if (uiAttachT64.exchange(false)) attachT64Image();
        if (uiAttachTAP.exchange(false)) attachTAPImage();

        // Input Menu
        if (uiToggleJoy1Req.exchange(false) && input)
            input->setJoystickAttached(1, !input->isJoy1Attached());

        if (uiToggleJoy2Req.exchange(false) && input)
            input->setJoystickAttached(2, !input->isJoy2Attached());

        // System Menu
        if (uiWarmReset.exchange(false)) warmReset();
        if (uiColdReset.exchange(false)) coldReset();
        if (uiEnterMonitor.exchange(false))
        {
            if (monitorCtl) monitorCtl->toggle();
        }

        int vm = uiVideoModeReq.exchange(-1);
        if (vm != -1) setVideoMode(vm ? "PAL" : "NTSC");

        switch (uiCass.exchange(CassCmd::None))
        {
            case CassCmd::Play:   if (cass) cass->play();   break;
            case CassCmd::Stop:   if (cass) cass->stop();   break;
            case CassCmd::Rewind: if (cass) cass->rewind(); break;
            case CassCmd::Eject:  if (cass) cass->eject();  break;
            default: break;
        }

        if (uiPaused.load())
        {
            IO_adapter->finishFrameAndSignal();
            auto now = std::chrono::steady_clock::now();
            if (now < nextFrameTime)
            {
                std::this_thread::sleep_until(nextFrameTime);
                nextFrameTime += frameDuration;
            }
            else
            {
                nextFrameTime = now + frameDuration;
            }
            continue; // skip CPU/SID/CIA work while paused
        }

        // Handle loading a PRG
        if (prgAttached && !prgLoaded && prgDelay <= 0)
        {
            // Load the program into memory
            loadPrgIntoMem();
            prgLoaded = true;
        }
        if (prgAttached && !prgLoaded && prgDelay > 0)
        {
            --prgDelay;
        }

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

bool Computer::loadPrgImage()
{
    std::ifstream prgFile(prgPath, std::ios::binary | std::ios::ate);
    if (!prgFile){
        return false;
    }

    std::streamsize size = prgFile.tellg();
    if (size < 3)
    {
        // Need at least 3 for load address (2) and data
        return false;
    }
    prgFile.seekg(0);
    prgImage.resize(size);
    prgFile.read(reinterpret_cast<char*>(prgImage.data()), size);
    return true;
}

void Computer::loadPrgIntoMem()
{
    // Skip the .P00 header if it is present
    size_t pos = 0;
    if (prgImage.size() >= 26 && std::memcmp(prgImage.data(), "C64File", 7) == 0)
    {
        pos = 26;  //  Skip wrapper
    }

    // First two bytes = load address
    if (pos + 2 > prgImage.size())
    {
        throw std::runtime_error("PRG/P00 image is too small.");
    }

    const uint16_t loadAddr = prgImage[pos] | (prgImage[pos + 1] << 8);
    pos += 2;

    const size_t programData = prgImage.size() - pos;
    uint32_t endProgramData = loadAddr + programData;

    if (endProgramData > 0x10000)
    {
        throw std::runtime_error("Error: Program is too large for 64k RAM!");
    }

    // Copy payload to C64 RAM
    for (size_t i = 0; i < programData; ++i)
    {
        mem->writeDirect(loadAddr + static_cast<uint16_t>(i), prgImage[pos + i]);
    }

    // BASIC program?  Fix pointers **only up to the 00 00 terminator**
    if (loadAddr == BASIC_PRG_START)
    {
        // Walk the BASIC line link chain until we find the terminator ($0000)
        uint16_t scan = loadAddr;
        uint16_t nextLine;
        do
        {
            nextLine = mem->read(scan) | (mem->read(scan + 1) << 8);
            if (nextLine == 0)
            {
                break;
            }
            scan = nextLine;
        }
        while (true);

        const uint16_t basicEnd = scan + 2;   // byte AFTER the 00 00 sentinel

        // Set pointers as KERNAL's LOAD does, using 8-bit direct writes
        mem->writeDirect(TXTAB, loadAddr & 0xFF);         // Low byte
        mem->writeDirect(TXTAB + 1, (loadAddr >> 8));     // High byte
        mem->writeDirect(VARTAB, basicEnd & 0xFF);
        mem->writeDirect(VARTAB + 1, (basicEnd >> 8));
        mem->writeDirect(ARYTAB, basicEnd & 0xFF);
        mem->writeDirect(ARYTAB + 1, (basicEnd >> 8));
        mem->writeDirect(STREND, basicEnd & 0xFF);
        mem->writeDirect(STREND + 1, (basicEnd >> 8));

        // Stuff “RUN<RETURN>” into the keyboard buffer
        const uint8_t runKeys[4] = { 0x52, 0x55, 0x4E, 0x0D };
        mem->writeDirect(0xC6, 4);
        for (int i = 0; i < 4; ++i)
        {
            mem->writeDirect(0x0277 + i, runKeys[i]);
        }
    }
}

void Computer::installMenu()
{
    IO_adapter->setGuiCallback([this]()
    {
        static bool aboutRequested = false;

        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Attach D64/D71 image...", "Ctrl+D"))
                {
                    startFileDialog("Select D64/D71 Image", { ".d64", ".d71"}, [this](const std::string path)
                    {
                        diskPath = path;
                        uiAttachD64 = true;
                    });
                }
                if (ImGui::MenuItem("Attach PRG/P00 image...", "Ctrl+P"))
                {
                    startFileDialog("Select PRG/P00 Image", { ".prg", ".p00" }, [this](const std::string path)
                    {
                        prgPath = path;
                        uiAttachPRG = true;
                    });
                }
                if (ImGui::MenuItem("Attach Cartridge image...", "Ctrl+C"))
                {
                    startFileDialog("Select CRT Image", { ".crt" }, [this](const std::string path)
                    {
                        cartridgePath = path;
                        uiAttachCRT = true;
                    });
                }
                if (ImGui::MenuItem("Attach T64 image...", "Ctrl+T"))
                {
                    startFileDialog("Select T64 image", { ".t64" }, [this](const std::string path)
                    {
                        tapePath = path;
                        uiAttachT64 = true;
                    });
                }
                if (ImGui::MenuItem("Attach TAP image...", "Ctrl+U"))
                {
                    startFileDialog("Select TAP image", { ".tap" }, [this](const std::string path)
                    {
                        tapePath = path;
                        uiAttachTAP = true;
                    });
                }
                ImGui::Separator();
                if (ImGui::BeginMenu("Cassette Control"))
                {
                    if (ImGui::MenuItem("Play", "Alt+P")) uiCass = CassCmd::Play;
                    if (ImGui::MenuItem("Stop", "Alt+S")) uiCass = CassCmd::Stop;
                    if (ImGui::MenuItem("Rewind", "Alt+R")) uiCass = CassCmd::Rewind;
                    if (ImGui::MenuItem("Eject", "Alt+E")) uiCass = CassCmd::Eject;
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Quit", "Alt+F4")) uiQuit = true;
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Input"))
            {
                bool j1 = input ? input->isJoy1Attached() : false;
                bool j2 = input ? input->isJoy2Attached() : false;
                if (ImGui::MenuItem("Joystick 1 Attached", nullptr, j1)) uiToggleJoy1Req = true;
                if (ImGui::MenuItem("Joystick 2 Attached", nullptr, j2)) uiToggleJoy2Req = true;
                ImGui::Separator();
                ImGui::Text("Gamepad Routing");

                SDL_GameController* p1 = input ? input->getPad1() : nullptr;
                SDL_GameController* p2 = input ? input->getPad2() : nullptr;

                auto padName = [&](SDL_GameController* p) -> const char*
                {
                    return p ? SDL_GameControllerName(p) : "None";
                };

                ImGui::Text("Pad1: %s", padName(p1));
                ImGui::Text("Pad2: %s", padName(p2));

                if (input && p1 && ImGui::MenuItem("Assign Pad1 -> Port 1")) input->assignPadToPort(p1, 1);
                if (input && p1 && ImGui::MenuItem("Assign Pad1 -> Port 2")) input->assignPadToPort(p1, 2);
                if (input && p2 && ImGui::MenuItem("Assign Pad2 -> Port 1")) input->assignPadToPort(p2, 1);
                if (input && p2 && ImGui::MenuItem("Assign Pad2 -> Port 2")) input->assignPadToPort(p2, 2);

                if (input && ImGui::MenuItem("Clear Port 1 Pad")) input->clearPortPad(1);
                if (input && ImGui::MenuItem("Clear Port 2 Pad")) input->clearPortPad(2);
                if (input && ImGui::MenuItem("Swap Port 1/2 Pads")) input->swapPortPads();
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("System"))
            {
                if (ImGui::MenuItem("Warm Reset", "Ctrl+W")) uiWarmReset = true;
                if (ImGui::MenuItem("Cold Reset", "Ctrl+Shift+R")) uiColdReset = true;
                bool isPAL = (videoMode_ == VideoMode::PAL);
                if (ImGui::MenuItem("NTSC", nullptr, !isPAL)) uiVideoModeReq = 0;
                if (ImGui::MenuItem("PAL",  nullptr,  isPAL)) uiVideoModeReq = 1;
                ImGui::Separator();
                bool paused = uiPaused.load();
                if (ImGui::MenuItem(paused ? "Resume" : "Pause", "Space")) uiPaused = !paused;
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Help")) {
                if (ImGui::MenuItem("About")) aboutRequested = true; // buffer
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        if (aboutRequested) { ImGui::OpenPopup("About C64 Emulator"); aboutRequested = false; }

        if (ImGui::BeginPopupModal("About C64 Emulator", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("C64 Emulator — ImGui Menu Overlay");
            ImGui::Separator();
            ImGui::Text("F12 opens ML Monitor.\nAlt+J, 1/2 attach joysticks.");
            if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        drawFileDialog();
    });
}

void Computer::startFileDialog(const std::string& title, std::vector<std::string> exts, std::function<void(const std::string&)> onAccept)
{
    fileDlg.open = true;
    fileDlg.title = title;
    fileDlg.allowedExtensions = std::move(exts);
    fileDlg.onAccept = std::move(onAccept);
    fileDlg.selectedEntry.clear();
    fileDlg.error.clear();
}

void Computer::drawFileDialog()
{
    if (!fileDlg.open)
        return;

    ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);

    const char* windowTitle = fileDlg.title.empty() ? "Select File" : fileDlg.title.c_str();

    if (!ImGui::Begin(windowTitle, &fileDlg.open))
    {
        ImGui::End();
        return;
    }

    namespace fs = std::filesystem;

    auto openPath = [this](const fs::path& path)
    {
        try
        {
            if (fs::is_directory(path))
            {
                fileDlg.currentDir    = path;
                fileDlg.selectedEntry.clear();
            }
            else
            {
                // Check extension against allowedExtensions (if not empty)
                std::string ext = path.extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

                bool allowed = fileDlg.allowedExtensions.empty();
                if (!allowed)
                {
                    for (const auto& a : fileDlg.allowedExtensions)
                    {
                        std::string lower = a;
                        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                        if (ext == lower)
                        {
                            allowed = true;
                            break;
                        }
                    }
                }

                if (!allowed)
                {
                    fileDlg.error = "File type not allowed for this action.";
                }
                else
                {
                    if (fileDlg.onAccept)
                    {
                        fileDlg.onAccept(path.string());
                    }
                    fileDlg.open = false;
                }
            }
        }
        catch (const std::exception& e)
        {
            fileDlg.error = e.what();
        }
    };

    std::string pathStr = fileDlg.currentDir.string();
    ImGui::TextUnformatted(pathStr.c_str());
    ImGui::Separator();

    ImGui::BeginChild("##file_list", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()*2), true);

    std::vector<fs::directory_entry> entries;
    fileDlg.error.clear();

    try
    {
        for (const auto& entry : fs::directory_iterator(fileDlg.currentDir))
            entries.push_back(entry);

        std::sort(entries.begin(), entries.end(),
                  [](const fs::directory_entry& a, const fs::directory_entry& b)
                  {
                      bool ad = a.is_directory();
                      bool bd = b.is_directory();
                      if (ad != bd) return ad;   // dirs first
                      return a.path().filename().string() < b.path().filename().string();
                  });
    }
    catch (const std::exception& e)
    {
        fileDlg.error = e.what();
    }

    // ".." to go up
     if (ImGui::Selectable("..", false, ImGuiSelectableFlags_AllowDoubleClick))
    {
        auto parent = fileDlg.currentDir.parent_path();
        if (!parent.empty())
        {
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                // Double-click: go up immediately
                fileDlg.currentDir    = parent;
                fileDlg.selectedEntry.clear();
            }
            else
            {
                // Single click: just go up too (or you could just select "..")
                fileDlg.currentDir    = parent;
                fileDlg.selectedEntry.clear();
            }
        }
    }

    for (const auto& entry : entries)
    {
        std::string name = entry.path().filename().string();
        bool isDir = entry.is_directory();

        // 🔹 Filter out files whose extension is not in allowedExtensions
        if (!isDir)
        {
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

            bool allowed = fileDlg.allowedExtensions.empty(); // if none, show all
            if (!allowed)
            {
                for (auto a : fileDlg.allowedExtensions)
                {
                    std::transform(a.begin(), a.end(), a.begin(), ::tolower);
                    if (ext == a)
                    {
                        allowed = true;
                        break;
                    }
                }
            }

            if (!allowed)
                continue; // skip drawing this file
        }

        std::string label   = isDir ? (name + "/") : name;
        bool selected       = (fileDlg.selectedEntry == name);

        if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick))
        {
            // Always update selection on click
            fileDlg.selectedEntry = name;

            // If it was a double-click, "open" the item immediately
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                fs::path path = fileDlg.currentDir / name;
                openPath(path);
            }
        }
    }

    ImGui::EndChild();

    if (!fileDlg.error.empty())
    {
        ImGui::TextColored(ImVec4(1, 0, 0, 1), "%s", fileDlg.error.c_str());
    }

    // Bottom buttons
    if (ImGui::Button("Up"))
    {
        auto parent = fileDlg.currentDir.parent_path();
        if (!parent.empty())
        {
            fileDlg.currentDir    = parent;
            fileDlg.selectedEntry.clear();
        }
    }

    ImGui::SameLine();

    if (ImGui::Button("Cancel"))
    {
        fileDlg.open = false;
        ImGui::End();
        return;
    }

    ImGui::SameLine();

    bool hasSelection = !fileDlg.selectedEntry.empty();
    if (!hasSelection)
        ImGui::BeginDisabled();

    if (ImGui::Button("Open"))
    {
        fs::path path = fileDlg.currentDir / fileDlg.selectedEntry;

        try
        {
            if (fs::is_directory(path))
            {
                fileDlg.currentDir    = path;
                fileDlg.selectedEntry.clear();
            }
            else
            {
                // Check extension against allowed list, if any
                std::string ext = path.extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

                bool allowed = fileDlg.allowedExtensions.empty();  // if none given, accept all
                if (!allowed)
                {
                    for (const auto& a : fileDlg.allowedExtensions)
                    {
                        if (ext == a)
                        {
                            allowed = true;
                            break;
                        }
                    }
                }

                if (!allowed)
                {
                    fileDlg.error = "File type not allowed for this action.";
                }
                else
                {
                    if (fileDlg.onAccept)
                    {
                        fileDlg.onAccept(path.string());
                    }
                    fileDlg.open = false;
                }
            }
        }
        catch (const std::exception& e)
        {
            fileDlg.error = e.what();
        }
    }

    if (!hasSelection)
        ImGui::EndDisabled();

    ImGui::End();
}

void Computer::attachD64Image()
{
    if (diskPath.empty())
    {
        std::cout << "No disk image selected.\n";
        return;
    }

    // Lazily create and register drive #8 if it does not exist yet
    if (!drive8)
    {
        drive8 = std::make_unique<D1541>(8, D1541LoROM, D1541HiROM);
        bus->registerDevice(8, drive8.get());
        drive8->reset();
    }

    if (drive8) drive8->loadDisk(diskPath);

    diskAttached = true;
}

void Computer::attachPRGImage()
{
    if (prgPath.empty()) return;

    prgAttached = true;
    prgLoaded   = false;
    prgDelay    = 140;

    if (!loadPrgImage())
    {
        std::cout << "Unable to load program: " << prgPath << "\n";
        prgAttached = false;
    }
    else
    {
        std::cout << "Queued program: " << prgPath << "\n";
    }
}

void Computer::attachCRTImage()
{
    if (cartridgePath.empty()) return;

    // Fully drop old cart state so we can't "reuse" its mapper/banks
    recreateCartridge();

    cartridgeAttached = true;

    if (!cart->loadROM(cartridgePath))
    {
        std::cout << "Unable to load cartridge: " << cartridgePath << "\n";
        cartridgeAttached = false;
        return;
    }

    if (mem) mem->setCartridgeAttached(true);
    if (pla) pla->setCartridgeAttached(true);

    coldReset();
    std::cout << "Cartridge attached: " << cartridgePath << "\n";
}

void Computer::attachT64Image()
{
    if (tapePath.empty()) return;

    tapeAttached = true;

    if (cass && !cass->loadCassette(tapePath, videoMode_))
    {
        #ifdef Debug
        std::cout << "Unable to load tape: " << tapePath << "\n";
        #endif
    }
    else
    {
        // T64 Auto-Load Logic
        if (cass && cass->isT64())
        {
            T64LoadResult result = cass->t64LoadPrgIntoMemory();
            if (result.success)
            {
                // 1. Calculate end of BASIC area (Assuming start at $0801)
                // We scan memory to find the end of the BASIC chain
                uint16_t scan = 0x0801;
                uint16_t nextLine;
                do {
                    nextLine = mem->read(scan) | (mem->read(scan + 1) << 8);
                    if (nextLine == 0) break;
                    scan = nextLine;
                } while (true);

                uint16_t basicEnd = scan + 2;

                // 2. Update BASIC Pointers so the C64 knows a program is loaded
                mem->writeDirect(0x2B, 0x01); mem->writeDirect(0x2C, 0x08); // TXTAB ($0801)
                mem->writeDirect(0x2D, basicEnd & 0xFF); mem->writeDirect(0x2E, basicEnd >> 8); // VARTAB
                mem->writeDirect(0x2F, basicEnd & 0xFF); mem->writeDirect(0x30, basicEnd >> 8); // ARYTAB
                mem->writeDirect(0x31, basicEnd & 0xFF); mem->writeDirect(0x32, basicEnd >> 8); // STREND

                // 3. Inject "RUN" + Return into keyboard buffer
                const uint8_t runKeys[4] = { 0x52, 0x55, 0x4E, 0x0D };
                mem->writeDirect(0xC6, 4); // Buffer size
                for (int i = 0; i < 4; ++i) mem->writeDirect(0x0277 + i, runKeys[i]);

                #ifdef Debug
                std::cout << "T64 attached and auto-loaded successfully.\n";
                #endif
            }
        }
    }
}

void Computer::attachTAPImage()
{
    if (tapePath.empty()) return;

    tapeAttached = true;
    if (cass && !cass->loadCassette(tapePath, videoMode_)) std::cout << "Unable to load tape: " << tapePath << "\n";
}

void Computer::recreateCartridge()
{
    cart = std::make_unique<Cartridge>();

    cart->attachCPUInstance(processor.get());
    cart->attachMemoryInstance(mem.get());
    cart->attachLogInstance(logger.get());
    cart->attachTraceManagerInstance(traceMgr.get());
    cart->attachVicInstance(vicII.get());

    // Make sure Memory/PLA/monitor backends now point at the new cart instance
    mem->attachCartridgeInstance(cart.get());
    pla->attachCartridgeInstance(cart.get());
    monbackend->attachCartridgeInstance(cart.get());
    traceMgr->attachCartInstance(cart.get());
}

bool Computer::isBASICReady()
{
    static const uint8_t readySC[6] = { 0x12, 0x05, 0x01, 0x04, 0x19, 0x2E };
    for (uint16_t addr = 0x0400; addr <= 0x07E8; ++addr)
    {   // last full 6 byte window
        bool match = true;
        for (int i = 0; i < 6; ++i)
            if (mem->read(addr + i) != readySC[i]) { match = false; break; }
        if (match) return true;
    }
    return false;
}

void Computer::debugBasicState()
{
    // Check if PC is at the main BASIC loop
    if (processor->getPC() == 0xA47B || isBASICReady())
    {
        printf("BASIC has booted! Dumping memory...\n");

        // Dump Zero Page
        for (int i = 0x0002; i < 0x0100; i++)
        {
            printf("Zero Page RAM[%04X] = %02X\n", i, mem->read(i));
        }

        // Dump BASIC Program Memory
        for (int i = 0x0800; i < 0x0820; i++)
        {
            printf("BASIC RAM[%04X] = %02X\n", i, mem->read(i));
        }

        // Dump System RAM Vectors
        for (int i = 0x0300; i < 0x0320; i++)
        {
            printf("System RAM[%04X] = %02X\n", i, mem->read(i));
        }

        // Dump IRQ Vector
        uint16_t irqVector = mem->read(0x0314) | (mem->read(0x0315) << 8);
        printf("IRQ Vector Address: %04X\n", irqVector);

        // Dump Stack
        printf("SP: %02X, Stack Top = %02X\n", processor->getSP(), mem->read(0x100 + processor->getSP()));

        // Dump CPU Fetch
        uint8_t opcode = mem->read(processor->getPC());
        printf("PC = %04X, Fetching Opcode = %02X\n", processor->getPC(), opcode);

        // Dump Screen Memory
        for (int i = 0x0400; i < 0x07FF; i++)
        {
            printf("Screen memory[%04X] = %02x\n", i, mem->read(i));
        }
    }
}
