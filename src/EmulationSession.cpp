// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include <thread>
#include "CPUTiming.h"
#include "DebugManager.h"
#include "EmulationSession.h"
#include "MachineComponents.h"
#include "MachineRomConfig.h"
#include "MachineRuntimeState.h"
#include "UIBridge.h"

EmulationSession::EmulationSession(MachineComponents& components,
                                   MachineRuntimeState& runtime,
                                   MachineRomConfig& roms,
                                   std::atomic<bool>& uiQuit)
    : components_(components),
      runtime_(runtime),
      roms_(roms),
      uiQuit_(uiQuit),
      cart_(*components.cart),
      cia1_(*components.cia1),
      cia2_(*components.cia2),
      cpu_(*components.cpu),
      debug_(*components.debug),
      ui_(*components.ui),
      bus_(*components.bus),
      inputMgr_(*components.inputMgr),
      inputRouter_(*components.inputRouter),
      io_(*components.io),
      logger_(*components.logger),
      media_(*components.media),
      mem_(*components.mem),
      pla_(*components.pla),
      sid_(*components.sid),
      uiBridge_(*components.uiBridge),
      vic_(*components.vic),
      frameDuration_(0.0),
      nextFrameTime_()
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

        if (!runtime_.running)
            break;
    }

    shutdown();
    return true;
}

bool EmulationSession::initializeMachine()
{
    if (!mem_.Initialize(roms_.basicRom, roms_.kernalRom, roms_.charRom))
    {
        throw std::runtime_error("Error: Problem encountered initializing memory!");
    }

    // Reset all chips
    bus_.reset();
    pla_.reset();
    cpu_.reset();
    vic_.reset();
    cia1_.reset();
    cia2_.reset();
    sid_.reset();

    // Process boot attachments
    media_.applyBootAttachments();

    // Start audio
    io_.playAudio();
    sid_.setSampleRate(io_.getSampleRate());

    // Show the ImGui menu
    io_.setGuiCallback([this]()
    {
        ui_.draw();
    });

    // Prime the renderer once up front
    io_.finishFrameAndSignal();
    io_.renderFrame(runtime_.running);

    frameDuration_ = std::chrono::duration<double, std::milli>(1000.0 / runtime_.cpuCfg->frameRate);
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
        io_.handleEvent(e, runtime_.running);

        // Then let InputRouter handle hotkeys, monitor toggle, etc.
        if (inputRouter_.handleEvent(e))
            continue;

        if (e.type == SDL_QUIT)
        {
            runtime_.running = false;
            uiQuit_ = true;
        }
    }

    debug_.tick();
    inputMgr_.tick();
}

bool EmulationSession::runFrame()
{
    if (runtime_.pendingBusPrime)
    {
        bus_.reset();
        runtime_.pendingBusPrime    = false;
        runtime_.busPrimedAfterBoot = true;
    }

    int frameCycles = 0;

    while (frameCycles < runtime_.cpuCfg->cyclesPerFrame())
    {
        uint32_t elapsedCycles = 0;

        if (vic_.getAEC())
        {
            try
            {
                uint16_t pc = cpu_.getPC();
                if (!runtime_.uiPaused.load() && debug_.hasBreakpoint(pc))
                {
                    runtime_.uiPaused = true;
                    debug_.onBreakpoint(pc);
                    break;
                }

                cpu_.tick();
                elapsedCycles = cpu_.getElapsedCycles();

                if (runtime_.uiPaused.load())
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

        sid_.tick(elapsedCycles);
        cia1_.updateTimers(elapsedCycles);
        cia2_.updateTimers(elapsedCycles);
        vic_.tick(elapsedCycles);
        bus_.tick(elapsedCycles);

        if (vic_.isFrameDone())
        {
            vic_.clearFrameFlag();
            io_.finishFrameAndSignal();
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
        runtime_.running = false;
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

    io_.renderFrame(runtime_.running);
    return true;
}

void EmulationSession::shutdown()
{
    runtime_.running = false;

    media_.flushAndSaveMedia();

    io_.stopRenderThread(runtime_.running);
    io_.setGuiCallback({});
}
