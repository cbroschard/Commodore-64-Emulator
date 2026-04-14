// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef EMULATION_SESSION_H
#define EMULATION_SESSION_H

#include <atomic>
#include <chrono>
#include <string>

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

struct MachineComponents;
struct MachineRuntimeState;
struct MachineRomConfig;

class EmulationSession
{
public:
    EmulationSession(MachineComponents& components,
                     MachineRuntimeState& runtime,
                     MachineRomConfig& roms,
                     std::atomic<bool>& uiQuit);
    ~EmulationSession();

    bool run();

private:
    bool initializeMachine();
    void processEvents();
    bool runFrame();
    bool finalizeFrame();
    void shutdown();

private:
    MachineComponents& components_;
    MachineRuntimeState& runtime_;
    MachineRomConfig& roms_;
    std::atomic<bool>& uiQuit_;

    // Cached hot references for debug-build performance
    Cartridge& cart_;
    CIA1& cia1_;
    CIA2& cia2_;
    CPU& cpu_;
    DebugManager& debug_;
    EmulatorUI& ui_;
    IECBUS& bus_;
    InputManager& inputMgr_;
    InputRouter& inputRouter_;
    IO& io_;
    Logging& logger_;
    MediaManager& media_;
    Memory& mem_;
    PLA& pla_;
    SID& sid_;
    UIBridge& uiBridge_;
    Vic& vic_;

    std::chrono::duration<double, std::milli> frameDuration_;
    std::chrono::steady_clock::time_point nextFrameTime_;
};

#endif // EMULATION_SESSION_H
