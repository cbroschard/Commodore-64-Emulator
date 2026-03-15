// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge.h"
#include "CIA1.h"
#include "CIA2.h"
#include "CPU.h"
#include "CPUTiming.h"
#include "DebugManager.h"
#include "EmulationSession.h"
#include "EmulatorUI.h"
#include "IECBUS.h"
#include "InputManager.h"
#include "InputRouter.h"
#include "IO.h"
#include "Logging.h"
#include "MediaManager.h"
#include "Memory.h"
#include "PLA.h"
#include "SID/SID.h"
#include "UIBridge.h"
#include "Vic.h"

EmulationSession::EmulationSession(Cartridge& cart,
                                   CIA1& cia1object,
                                   CIA2& cia2object,
                                   CPU& processor,
                                   DebugManager& debug,
                                   EmulatorUI& ui,
                                   IECBUS& bus,
                                   InputManager& inputMgr,
                                   InputRouter& inputRouter,
                                   IO& ioAdapter,
                                   Logging& logger,
                                   MediaManager& media,
                                   Memory& mem,
                                   PLA& pla,
                                   SID& sidchip,
                                   UIBridge& uiBridge,
                                   Vic& vicII,
                                   const CPUConfig*& cpuCfg,
                                   bool& pendingBusPrime,
                                   bool& busPrimedAfterBoot,
                                   const std::string& basicRom,
                                   const std::string& kernalRom,
                                   const std::string& charRom,
                                   std::atomic<bool>& running,
                                   std::atomic<bool>& uiQuit,
                                   std::atomic<bool>& uiPaused)
    : cart_(cart),
      cia1object_(cia1object),
      cia2object_(cia2object),
      processor_(processor),
      debug_(debug),
      ui_(ui),
      bus_(bus),
      inputMgr_(inputMgr),
      inputRouter_(inputRouter),
      ioAdapter_(ioAdapter),
      logger_(logger),
      media_(media),
      mem_(mem),
      pla_(pla),
      sidchip_(sidchip),
      uiBridge_(uiBridge),
      vicII_(vicII),
      running_(running),
      uiQuit_(uiQuit),
      uiPaused_(uiPaused),
      frameDuration_(0.0),
      nextFrameTime_(),
      cpuCfg_(cpuCfg),
      pendingBusPrime_(pendingBusPrime),
      busPrimedAfterBoot_(busPrimedAfterBoot),
      basicRom_(basicRom),
      kernalRom_(kernalRom),
      charRom_(charRom)
{

}

EmulationSession::~EmulationSession() = default;

bool EmulationSession::run()
{
    if (!initializeMachine())
        return false;

    while (true)
    {
        processEvents();

        if (!runFrame())
            break;

        if (!finalizeFrame())
            break;

        if (!running_)
            break;
    }

    shutdown();
    return true;
}

bool EmulationSession::initializeMachine()
{
    if (!mem_.Initialize(basicRom_, kernalRom_, charRom_))
    {
        throw std::runtime_error("Error: Problem encountered initializing memory!");
    }

    // Reset all chips
    bus_.reset();
    pla_.reset();
    processor_.reset();
    vicII_.reset();
    cia1object_.reset();
    cia2object_.reset();
    sidchip_.reset();

    // Process boot attachments
    media_.applyBootAttachments();

    // Start audio
    ioAdapter_.playAudio();
    sidchip_.setSampleRate(ioAdapter_.getSampleRate());

    // Show the ImGui menu
    ioAdapter_.setGuiCallback([this]()
    {
        ui_.draw();
    });

    // Prime the renderer once up front
    ioAdapter_.finishFrameAndSignal();
    ioAdapter_.renderFrame(running_);

    frameDuration_ = std::chrono::duration<double, std::milli>(1000.0 / cpuCfg_->frameRate);
    nextFrameTime_ = std::chrono::steady_clock::now() +
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(frameDuration_);

    return true;
}

void EmulationSession::processEvents()
{
    const bool wantTextInput = debug_.monitorController().isOpen() || ui_.isFileDialogOpen();

    if (wantTextInput)
        SDL_StartTextInput();
    else
        SDL_StopTextInput();

    SDL_Event e;
    while (SDL_PollEvent(&e))
    {
        // Let IO handle ImGui + main-thread window/input events
        ioAdapter_.handleEvent(e, running_);

        // Then let InputRouter handle hotkeys, monitor toggle, etc.
        if (inputRouter_.handleEvent(e))
            continue;

        if (e.type == SDL_QUIT)
        {
            running_ = false;
            uiQuit_ = true;
        }
    }

    debug_.tick();
    inputMgr_.tick();
}

bool EmulationSession::runFrame()
{
    if (pendingBusPrime_)
    {
        bus_.reset();
        pendingBusPrime_    = false;
        busPrimedAfterBoot_ = true;
    }

    int frameCycles = 0;

    while (frameCycles < cpuCfg_->cyclesPerFrame())
    {
        uint32_t elapsedCycles = 0;

        if (vicII_.getAEC())
        {
            try
            {
                uint16_t pc = processor_.getPC();
                if (!uiPaused_.load() && debug_.hasBreakpoint(pc))
                {
                    uiPaused_ = true;
                    debug_.onBreakpoint(pc);
                    break;
                }

                processor_.tick();
                elapsedCycles = processor_.getElapsedCycles();

                if (uiPaused_.load())
                    break;
            }
            catch (const std::exception& e)
            {
                std::cerr << "Exception caught: " << e.what() << "\n";
                logger_.flush();
                return false;
            }
        }
        else
        {
            // AEC low: CPU idle, VIC still runs
            elapsedCycles = 1;
        }

        sidchip_.tick(elapsedCycles);
        cia1object_.updateTimers(elapsedCycles);
        cia2object_.updateTimers(elapsedCycles);
        vicII_.tick(elapsedCycles);
        bus_.tick(elapsedCycles);

        if (vicII_.isFrameDone())
        {
            vicII_.clearFrameFlag();
            ioAdapter_.finishFrameAndSignal();
        }

        if (auto* mapper = cart_.getMapper())
            mapper->tick(elapsedCycles);

        frameCycles += static_cast<int>(elapsedCycles);
    }

    return true;
}

bool EmulationSession::finalizeFrame()
{
    if (uiQuit_.exchange(false))
    {
        running_ = false;
        return false;
    }

    ui_.setMediaViewState(uiBridge_.buildMediaViewState());

    auto now = std::chrono::steady_clock::now();
    const auto frameStep =
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(frameDuration_);

    if (now < nextFrameTime_)
    {
        std::this_thread::sleep_until(nextFrameTime_);
    }
    do
    {
        nextFrameTime_ += frameStep;
    }
    while (nextFrameTime_ <= now);

    uiBridge_.processCommands();
    media_.tick();

    ioAdapter_.renderFrame(running_);
    return true;
}

void EmulationSession::shutdown()
{
    running_ = false;

    ioAdapter_.stopRenderThread(running_);
    ioAdapter_.setGuiCallback({});
}
