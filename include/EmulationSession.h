// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef EMULATIONSESSION_H
#define EMULATIONSESSION_H

#include <atomic>
#include <chrono>
#include <thread>

struct MachineComponents;
struct MachineRuntimeState;
struct MachineRomConfig;

class EmulationSession
{
    public:
        EmulationSession(MachineComponents& components, MachineRuntimeState& runtime, MachineRomConfig& roms, std::atomic<bool>& uiQuit);

        ~EmulationSession();

        bool run();

    protected:

    private:
        MachineComponents& components_;
        MachineRuntimeState& runtime_;
        MachineRomConfig& roms_;

        std::atomic<bool>& uiQuit_;

        std::chrono::duration<double, std::milli> frameDuration_;
        std::chrono::steady_clock::time_point nextFrameTime_;

        // Emulation phases
        bool initializeMachine();
        void processEvents();
        bool runFrame();
        bool finalizeFrame();
        void shutdown();
};

#endif // EMULATIONSESSION_H
