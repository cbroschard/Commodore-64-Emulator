// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "CPUTiming.h"
#include "DebugManager.h"
#include "EmulationSession.h"
#include "MachineRomConfig.h"
#include "MachineComponents.h"
#include "MachineRuntimeState.h"
#include "UIBridge.h"

EmulationSession::EmulationSession(MachineComponents& components, MachineRuntimeState& runtime, MachineRomConfig& roms, std::atomic<bool>& uiQuit)
    : components_(components),
      runtime_(runtime),
      roms_(roms),
      uiQuit_(uiQuit),
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
    if (!components_.mem->Initialize(roms_.basicRom, roms_.kernalRom, roms_.charRom))
    {
        throw std::runtime_error("Error: Problem encountered initializing memory!");
    }

    // Reset all chips
    components_.bus->reset();
    components_.pla->reset();
    components_.cpu->reset();
    components_.vic->reset();
    components_.cia1->reset();
    components_.cia2->reset();
    components_.sid->reset();

    // Process boot attachments
    components_.media->applyBootAttachments();

    // Start audio
    components_.io->playAudio();
    components_.sid->setSampleRate(components_.io->getSampleRate());

    // Show the ImGui menu
    components_.io->setGuiCallback([this]()
    {
        components_.ui->draw();
    });

    // Prime the renderer once up front
    components_.io->finishFrameAndSignal();
    components_.io->renderFrame(runtime_.running);

    frameDuration_ = std::chrono::duration<double, std::milli>(1000.0 / runtime_.cpuCfg->frameRate);
    nextFrameTime_ = std::chrono::steady_clock::now() +
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(frameDuration_);

    return true;
}

void EmulationSession::processEvents()
{
    const bool wantTextInput = components_.debug->monitorController().isOpen() || components_.ui->isFileDialogOpen();

    if (wantTextInput)
        SDL_StartTextInput();
    else
        SDL_StopTextInput();

    SDL_Event e;
    while (SDL_PollEvent(&e))
    {
        // Let IO handle ImGui + main-thread window/input events
        components_.io->handleEvent(e, runtime_.running);

        // Then let InputRouter handle hotkeys, monitor toggle, etc.
        if (components_.inputRouter->handleEvent(e))
            continue;

        if (e.type == SDL_QUIT)
        {
            runtime_.running = false;
            uiQuit_ = true;
        }
    }

    components_.debug->tick();
    components_.inputMgr->tick();
}

bool EmulationSession::runFrame()
{
    if (runtime_.pendingBusPrime)
    {
        components_.bus->reset();
        runtime_.pendingBusPrime    = false;
        runtime_.busPrimedAfterBoot = true;
    }

    int frameCycles = 0;

    while (frameCycles < runtime_.cpuCfg->cyclesPerFrame())
    {
        uint32_t elapsedCycles = 0;

        if (components_.vic->getAEC())
        {
            try
            {
                uint16_t pc = components_.cpu->getPC();
                if (!runtime_.uiPaused.load() && components_.debug->hasBreakpoint(pc))
                {
                    runtime_.uiPaused = true;
                    components_.debug->onBreakpoint(pc);
                    break;
                }

                components_.cpu->tick();
                elapsedCycles = components_.cpu->getElapsedCycles();

                if (runtime_.uiPaused.load())
                    break;
            }
            catch (const std::exception& e)
            {
                std::cerr << "Exception caught: " << e.what() << "\n";
                components_.logger->flush();
                return false;
            }
        }
        else
        {
            // AEC low: CPU idle, VIC still runs
            elapsedCycles = 1;
        }

        components_.sid->tick(elapsedCycles);
        components_.cia1->updateTimers(elapsedCycles);
        components_.cia2->updateTimers(elapsedCycles);
        components_.vic->tick(elapsedCycles);
        components_.bus->tick(elapsedCycles);

        if (components_.vic->isFrameDone())
        {
            components_.vic->clearFrameFlag();
            components_.io->finishFrameAndSignal();
        }

        if (auto* mapper = components_.cart->getMapper())
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

    components_.ui->setMediaViewState(components_.uiBridge->buildMediaViewState());

    auto now = std::chrono::steady_clock::now();
    const auto frameStep = std::chrono::duration_cast<std::chrono::steady_clock::duration>(frameDuration_);

    if (now < nextFrameTime_)
    {
        std::this_thread::sleep_until(nextFrameTime_);
    }
    do
    {
        nextFrameTime_ += frameStep;
    }
    while (nextFrameTime_ <= now);

    components_.uiBridge->processCommands();
    components_.media->tick();

    components_.io->renderFrame(runtime_.running);
    return true;
}

void EmulationSession::shutdown()
{
    runtime_.running = false;

    components_.io->stopRenderThread(runtime_.running);
    components_.io->setGuiCallback({});
}
