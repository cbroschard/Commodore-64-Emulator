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
    ui(std::make_unique<EmulatorUI>()),
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
    if (input) input->setJoystickAttached(port, flag);
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
            if (sc == SDL_SCANCODE_P) { if (cass) cass->play();   return true; }
            if (sc == SDL_SCANCODE_S) { if (cass) cass->stop();   return true; }
            if (sc == SDL_SCANCODE_R) { if (cass) cass->rewind(); return true; }
            if (sc == SDL_SCANCODE_E) { if (cass) cass->eject();  return true; }
        }
    }

    // Feed InputManager (keyboard/joystick mapping)
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
    bus->reset();
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
    bus->reset();
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
            // handle hotkeys / monitor / input mapping first
            if (handleInputEvent(e))
                continue;

            // Controller add/remove
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
        if (input) input->tick();

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

        if (ui)
        {
            ui->setMediaViewState(buildUIState());
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
        processUICommands();
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

std::string Computer::lowerExt(const std::string& path)
{
    auto dot = path.find_last_of('.');
    std::string ext = (dot == std::string::npos) ? "" : path.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c){ return (char)std::tolower(c); });
    return ext;
}

bool Computer::isExtCompatible(UiCommand::DriveType driveType, const std::string& ext)
{
    switch (driveType)
    {
        case UiCommand::DriveType::D1541: return ext == ".d64";
        case UiCommand::DriveType::D1571: return (ext == ".d64" || ext == ".d71");
        case UiCommand::DriveType::D1581: return ext == ".d81";
    }
    return false;
}

void Computer::attachDiskImage(int deviceNum, UiCommand::DriveType driveType, const std::string& path)
{
    if (path.empty()) return;
    if (deviceNum < 8 || deviceNum > 11) return;

    const std::string ext = lowerExt(path);
    if (!isExtCompatible(driveType, ext))
    {
        std::cout << "Incompatible disk image for selected drive type.\n";
        std::cout << "Drive " << deviceNum << " type=" << (int)driveType
                  << " path=" << path << "\n";
        return;
    }

    if (!drives[deviceNum])
    {
        switch (driveType)
        {
            case UiCommand::DriveType::D1541:
                drives[deviceNum] = std::make_unique<D1541>(deviceNum, D1541LoROM, D1541HiROM);
                break;

            case UiCommand::DriveType::D1571:
                drives[deviceNum] = std::make_unique<D1571>(deviceNum, D1571ROM);
                break;

            case UiCommand::DriveType::D1581:
                drives[deviceNum] = std::make_unique<D1581>(deviceNum, D1581ROM);
                break;
        }

        bus->registerDevice(deviceNum, drives[deviceNum].get());
        // Sync all existing devices so nobody has stale cached bus state
        for (int dev = 8; dev <= 11; ++dev)
        {
            if (drives[dev]) drives[dev]->forceSyncIEC();
        }
        if (!busPrimedAfterBoot)
            pendingBusPrime = true;
    }

    if (!drives[deviceNum]->insert(path))
    {
        std::cout << "Disk insert failed: " << path << "\n";
        return;
    }

    diskAttached = true;
    diskPath = "Drive " + std::to_string(deviceNum) + ": " + path;
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

EmulatorUI::MediaViewState Computer::buildUIState() const
{
    EmulatorUI::MediaViewState s;

    s.diskAttached = diskAttached;
    s.diskPath     = diskPath;

    s.cartAttached = cartridgeAttached;
    s.cartPath     = cartridgePath;

    s.tapeAttached = tapeAttached;
    s.tapePath     = tapePath;

    s.prgAttached  = prgAttached;
    s.prgPath      = prgPath;

    if (input)
    {
        s.joy1Attached = input->isJoy1Attached();
        s.joy2Attached = input->isJoy2Attached();

        auto p1 = input->getPad1();
        auto p2 = input->getPad2();

        s.pad1Name = p1 ? SDL_GameControllerName(p1) : "None";
        s.pad2Name = p2 ? SDL_GameControllerName(p2) : "None";
    }
    else
    {
        s.joy1Attached = false;
        s.joy2Attached = false;
        s.pad1Name = "None";
        s.pad2Name = "None";
    }

    s.paused = uiPaused.load();
    s.pal    = (videoMode_ == VideoMode::PAL);

    return s;
}

void Computer::processUICommands()
{
    for (const auto& cmd : ui->consumeCommands())
    {
        switch (cmd.type)
        {
            case UiCommand::Type::AttachDisk:
                attachDiskImage(cmd.deviceNum, cmd.driveType, cmd.path);
                break;

            case UiCommand::Type::AttachPRG:
                prgPath = cmd.path;
                attachPRGImage();
                break;

            case UiCommand::Type::AttachCRT:
                cartridgePath = cmd.path;
                attachCRTImage();
                break;

            case UiCommand::Type::AttachT64:
                tapePath = cmd.path;
                attachT64Image();
                break;

            case UiCommand::Type::AttachTAP:
                tapePath = cmd.path;
                attachTAPImage();
                break;

            case UiCommand::Type::WarmReset:
                warmReset();
                break;

            case UiCommand::Type::ColdReset:
                coldReset();
                break;

            case UiCommand::Type::SetPAL:
                setVideoMode("PAL");
                break;

            case UiCommand::Type::SetNTSC:
                setVideoMode("NTSC");
                break;

            case UiCommand::Type::TogglePause:
                uiPaused = !uiPaused.load();
                break;

            case UiCommand::Type::ToggleJoy1:
                if (input) input->setJoystickAttached(1, !input->isJoy1Attached());
                break;

            case UiCommand::Type::ToggleJoy2:
                if (input) input->setJoystickAttached(2, !input->isJoy2Attached());
                break;

            case UiCommand::Type::AssignPad1ToPort1:
                if (input && input->getPad1()) input->assignPadToPort(input->getPad1(), 1);
                break;

            case UiCommand::Type::AssignPad1ToPort2:
                if (input && input->getPad1()) input->assignPadToPort(input->getPad1(), 2);
                break;

            case UiCommand::Type::AssignPad2ToPort1:
                if (input && input->getPad2()) input->assignPadToPort(input->getPad2(), 1);
                break;

            case UiCommand::Type::AssignPad2ToPort2:
                if (input && input->getPad2()) input->assignPadToPort(input->getPad2(), 2);
                break;

            case UiCommand::Type::ClearPort1Pad:
                if (input) input->clearPortPad(1);
                break;

            case UiCommand::Type::ClearPort2Pad:
                if (input) input->clearPortPad(2);
                break;

            case UiCommand::Type::SwapPortPads:
                if (input) input->swapPortPads();
                break;

            case UiCommand::Type::CassPlay:
                if (cass) cass->play();
                break;

            case UiCommand::Type::CassStop:
                if (cass) cass->stop();
                break;

            case UiCommand::Type::CassRewind:
                if (cass) cass->rewind();
                break;

            case UiCommand::Type::CassEject:
                if (cass) cass->eject();
                break;

            case UiCommand::Type::EnterMonitor:
                enterMonitor();
                break;

            case UiCommand::Type::Quit:
                running = false;
                break;

            default:
                break;
        }
    }
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
