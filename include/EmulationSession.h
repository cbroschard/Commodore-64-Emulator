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

class Cartridge;
class CIA1;
class CIA2;
class CPU;
class DebugManager;
class EmulatorUI;
class IECBUS;
class InputManager;
class InputRouter;
class IO;
class Logging;
class MediaManager;
class Memory;
class PLA;
class SID;
class UIBridge;
class Vic;

struct CPUConfig;

class EmulationSession
{
    public:
        EmulationSession(Cartridge& cart,
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
                     std::atomic<bool>& uiPaused);

        ~EmulationSession();

        bool run();

    protected:

    private:
        Cartridge& cart_;
        CIA1& cia1object_;
        CIA2& cia2object_;
        CPU& processor_;
        DebugManager& debug_;
        EmulatorUI& ui_;
        IECBUS& bus_;
        InputManager& inputMgr_;
        InputRouter& inputRouter_;
        IO& ioAdapter_;
        Logging& logger_;
        MediaManager& media_;
        Memory& mem_;
        PLA& pla_;
        SID& sidchip_;
        UIBridge& uiBridge_;
        Vic& vicII_;

        std::atomic<bool>& running_;
        std::atomic<bool>& uiQuit_;
        std::atomic<bool>& uiPaused_;

        std::chrono::duration<double, std::milli> frameDuration_;
        std::chrono::steady_clock::time_point nextFrameTime_;

        const CPUConfig*& cpuCfg_;
        bool& pendingBusPrime_;
        bool& busPrimedAfterBoot_;
        const std::string& basicRom_;
        const std::string& kernalRom_;
        const std::string& charRom_;

        // Emulation phases
        bool initializeMachine();
        void processEvents();
        bool runFrame();
        bool finalizeFrame();
        void shutdown();
};

#endif // EMULATIONSESSION_H
