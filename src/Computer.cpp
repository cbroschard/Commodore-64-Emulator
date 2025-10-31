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
    IO_adapter(std::make_unique<IO>()),
    IRQ(std::make_unique<IRQLine>()),
    keyb(std::make_unique<Keyboard>()),
    logger(std::make_unique<Logging>("debug.txt")),
    mem(std::make_unique<Memory>()),
    monitor(std::make_unique<MLMonitor>()),
    monbackend(std::make_unique<MLMonitorBackend>()),
    pla(std::make_unique<PLA>()),
    sidchip(std::make_unique<SID>(44100)),
    traceMgr(std::make_unique<TraceManager>()),
    vicII(std::make_unique<Vic>()),
    showMonitorOverlay(false),
    prgDelay(140),
    videoMode_(VideoMode::NTSC),
    cpuCfg_(&NTSC_CPU),
    cartridgeAttached(false),
    cartridgePath(""),
    tapeAttached(false),
    tapePath(""),
    t64Injected(false),
    prgAttached(false),
    prgLoaded(false),
    prgPath(""),
    diskAttached(false),
    diskPath(""),
    joystick1Attached(false),
    joystick2Attached(false),
    frameReady(false),
    running(true)
{
    // Attach components to each other
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

    processor->attachMemoryInstance(mem.get());
    processor->attachCIA2Instance(cia2object.get());
    processor->attachVICInstance(vicII.get());
    processor->attachIRQLineInstance(IRQ.get());
    processor->attachLogInstance(logger.get());
    processor->attachTraceManagerInstance(traceMgr.get());

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

    vicII->attachIOInstance(IO_adapter.get());
    vicII->attachCPUInstance(processor.get());
    vicII->attachMemoryInstance(mem.get());
    vicII->attachCIA2Instance(cia2object.get());
    vicII->attachIRQLineInstance(IRQ.get());
    vicII->attachLogInstance(logger.get());

    IO_adapter->attachVICInstance(vicII.get());
    IO_adapter->attachSIDInstance(sidchip.get());
    IO_adapter->attachLogInstance(logger.get());

    pla->attachCartridgeInstance(cart.get());
    pla->attachCPUInstance(processor.get());
    pla->attachLogInstance(logger.get());
    pla->attachTraceManagerInstance(traceMgr.get());
    pla->attachVICInstance(vicII.get());

    cart->attachCPUInstance(processor.get());
    cart->attachMemoryInstance(mem.get());
    cart->attachLogInstance(logger.get());
    cart->attachTraceManagerInstance(traceMgr.get());
    cart->attachVicInstance(vicII.get());

    cass->attachMemoryInstance(mem.get());
    cass->attachLogInstance(logger.get());

    keyb->attachLogInstance(logger.get());

    sidchip->attachCPUInstance(processor.get());;
    sidchip->attachLogInstance(logger.get());
    sidchip->attachTraceManagerInstance(traceMgr.get());
    sidchip->attachVicInstance(vicII.get());

    bus->attachCIA2Instance(cia2object.get());
    bus->attachLogInstance(logger.get());
}

Computer::~Computer() = default;

void Computer::setJoystickAttached(int port, bool flag)
{
    switch(port)
    {
        case 1:
        {
            if (flag)
            {
                joystick1Attached = true;
                joy1 = std::make_unique<Joystick>(1);
                cia1object->attachJoystickInstance(joy1.get());
            }
            else
            {
                joystick1Attached = false;
                try
                {
                    cia1object->detachJoystickInstance(joy1.get());
                }
                catch(const std::runtime_error& error)
                {
                    std::cout << "Caught exception: " << error.what() << std::endl;
                }
                catch(...)
                {
                    std::cout << "Caught unknown Joystick exception!" << std::endl;
                }
                joy1.reset();
            }
            break;
        }
        case 2:
        {
            if (flag)
            {
                joystick2Attached = true;
                joy2 = std::make_unique<Joystick>(2);
                cia1object->attachJoystickInstance(joy2.get());
            }
            else
            {
                joystick2Attached = false;
                try
                {
                    cia1object->detachJoystickInstance(joy2.get());
                }
                catch(const std::runtime_error& error)
                {
                    std::cout << "Caught exception: " << error.what() << std::endl;
                }
                catch(...)
                {
                    std::cout << "Caught unknown Joystick exception!" << std::endl;
                }
                joy2.reset();
            }
            break;
        }
        default:
        {
            break;
        }
    }
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

bool Computer::handleInputEvent(const SDL_Event& ev)
{
    // Enter ML Monitor
    if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_F12)
    {
        monitor->enter();
        //showMonitorOverlay = !showMonitorOverlay;
        return true;
    }

    // Only care about key-down/up (ignore auto-repeats)
    if ((ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) && !ev.key.repeat)
    {
        auto sc   = ev.key.keysym.scancode;
        bool down = (ev.type == SDL_KEYDOWN);

        // current modifier state + full key-state snapshot
        auto mods = SDL_GetModState();
        const auto* ks = SDL_GetKeyboardState(nullptr);

        if (down && (mods & KMOD_ALT))
        {
            if (sc == SDL_SCANCODE_E)
            {
                cass->eject();
                return true;
            }
            if (sc == SDL_SCANCODE_P)
            {
                cass->play();
                return true;
            }
            if (sc == SDL_SCANCODE_R)
            {
                cass->rewind();
                return true;
            }
            if (sc == SDL_SCANCODE_S)
            {
                cass->stop();
                return true;
            }
        }

        // If Alt is down and the key is J, 1 or 2, swallow it unconditionally
        if ((mods & KMOD_ALT) && (sc == SDL_SCANCODE_J || sc == SDL_SCANCODE_1 || sc == SDL_SCANCODE_2))
        {
            if (down && ks[SDL_SCANCODE_J])  // only on the key-down of 1 or 2
            {
                if (sc == SDL_SCANCODE_1)
                {
                    setJoystickAttached(1, !joystick1Attached);
                }
                else if (sc == SDL_SCANCODE_2)
                {
                    setJoystickAttached(2, !joystick2Attached);
                }
            }
            return true;  // never pass J,1,2 through to the C-64
        }

        // Joystick-direction keys
        for (int port = 1; port <= 2; ++port)
        {
            auto& joyPtr = (port == 1 ? joy1 : joy2);
            if (!joyPtr)
            {
                continue;
            }

            auto it = joyMap[port].find(sc);
            if (it != joyMap[port].end())
            {
                uint8_t state = joyPtr->getState();
                if (down)
                {
                    state &= ~it->second;  // clear bit (pressed)
                }
                else
                {
                    state |=  it->second;  // set bit (released)
                }
                joyPtr->setState(state);
                return true;
            }
        }

        // Fall back to the C-64 keyboard
        if (down)
        {
            keyb->handleKeyDown(sc);
        }
        else
        {
            keyb->handleKeyUp(sc);
        }
        return true;
    }

    return false;  // not a key event we care about
}

void Computer::setJoystickConfig(int port, JoystickMapping& cfg)
{
    // clear any existing mapping for this port
    joyMap[port].clear();

    // rebuild the mapping using config scancodes
    joyMap[port] = {
        { cfg.up,    Joystick::direction::up },
        { cfg.down,  Joystick::direction::down },
        { cfg.left,  Joystick::direction::left },
        { cfg.right, Joystick::direction::right },
        { cfg.fire,  Joystick::direction::button }
    };
}

Joystick* Computer::getJoy1()
{
    if (joy1) return joy1.get();
    else return nullptr;
}

Joystick* Computer::getJoy2()
{
    if (joy2) return joy2.get();
    else return nullptr;
}

void Computer::warmReset()
{
    std::cout << "Performing warm reset...\n";

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
    std::cout << "Performing cold reset...\n";

    if (!cartridgeAttached && cart)
    {
        cart->setGameLine(true);
        cart->setExROMLine(true);
    }

    if (!mem->Initialize(BASIC_ROM, KERNAL_ROM, CHAR_ROM))
    {
        throw std::runtime_error("Error: Problem encountered initializing memory!");
    }

    // Reset memory control register to default
    pla->reset();

    // Reset major chips
    vicII->reset();
    cia1object->reset();
    cia2object->reset();
    sidchip->reset();

    // Reset CPU (reloads PC from $FFFC/$FFFD)
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
            std::cout << "Unable to load cartridge: " << cartridgePath << std::endl;
            // Load failed, exit completely
            return false;
        }
    }
    else if (tapeAttached)
    {
        if (!cass->loadCassette(tapePath, videoMode_))
        {
            std::cout << "Unable to load tape: " << tapePath << std::endl;
        }
        if (cass->isT64())
        {
            cass->play(); // Press play immediately for t64 files
        }
    }
    else if (prgAttached)
    {
        if (!loadPrgImage())
        {
            std::cout << "Unable to load program: " << prgPath << std::endl;
        }
        else
        {
            std::cout << "Successfully loaded the file: " << prgPath << std::endl;
        }
    }

    // **Start Audio Playback**
    IO_adapter->playAudio();
    sidchip->setSampleRate(IO_adapter->getSampleRate());

    /*// Check for monitor hotkey
    IO_adapter->setGuiCallback([this]()
    {
        if (!showMonitorOverlay) return;

        // Minimal on-screen monitor
        ImGui::SetNextWindowSize(ImVec2(1000, 600), ImGuiCond_Always);
        ImGui::SetNextWindowPos(ImVec2(40, 40), ImGuiCond_Always);
        ImGui::Begin("C64 Monitor");
        ImGui::Text("PC: $%04X   A:$%02X  X:$%02X  Y:$%02X  SP:$%02X",
            processor->getPC(), processor->getA(), processor->getX(),
            processor->getY(), processor->getSP());

        static char cmd[256] = {};
        if (ImGui::InputText("monitor>", cmd, IM_ARRAYSIZE(cmd),
                             ImGuiInputTextFlags_EnterReturnsTrue))
        {
            monitor->handleCommand(std::string(cmd));
            cmd[0] = '\0';
        }
        ImGui::End();
    });*/

    // Graphics rendering thread
    IO_adapter->startRenderThread(running);
    IO_adapter->finishFrameAndSignal();

    const auto frameDuration = std::chrono::duration<double, std::milli>(1000.0 / cpuCfg_->frameRate);

    SDL_Event event;
    auto nextFrameTime = std::chrono::steady_clock::now() + frameDuration;

    while (true)
    {
        int frameCycles = 0;

        while (SDL_PollEvent(&event))
            {
                if (event.type == SDL_QUIT)
                {
                    running = false;
                    IO_adapter->stopRenderThread(running);
                    return true; // Exit the emulator
                }
                if (handleInputEvent(event))
                {
                    continue;
                }
            }

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
                        std::cout << "Hit breakpoint at $" << std::uppercase << std::hex << std::setw(4) << std::setfill('0')
                            << pc << std::endl;
                        monitor->enter();
                    }

                    // Execute one CPU instruction
                    processor->tick();
                    elapsedCycles = processor->getElapsedCycles();

                    // F4A5 equals Kernal tape loader routine
                    if (processor->getPC() == 0xF4A5 && !t64Injected && cass->isT64())
                    {
                        cass->stop();
                        T64LoadResult result = cass->t64LoadPrgIntoMemory();
                            if (!result.success)
                            {
                                std::cout << "Failed to load PRG from T64!" << std::endl;
                                processor->setA(4); // Standard KERNAL read error code
                                processor->setFlag(CPU::C, true); // Set Carry to indicate error
                            }
                            else
                            {
                                processor->setA(0); // Status OK
                                processor->setX(result.prgEnd & 0xFF); // Low byte of program end address
                                processor->setY((result.prgEnd >> 8) & 0xFF); // High byte of program end address
                                processor->setFlag(CPU::C, false); // No error
                                processor->rtsFromQuickLoad();
                            }
                            t64Injected = true;
                    }
                }
                catch (const std::exception& e)
                {
                    std::cerr << "Exception caught: " << e.what() << std::endl;
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

            if (vicII->isFrameDone())
            {
                vicII->clearFrameFlag();
                IO_adapter->finishFrameAndSignal();   // hand frame to render thread
            }

            // Add elapsed cycles to the frame count
            frameCycles += elapsedCycles;
        }

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
