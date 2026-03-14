// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef STATEMANAGER_H
#define STATEMANAGER_H

#include <array>
#include <atomic>
#include <memory>
#include <string>
#include "Common/VideoMode.h"
#include "StateReader.h"
#include "StateWriter.h"

class Cartridge;
class Cassette;
class CIA1;
class CIA2;
class CPU;
class Drive;
class IECBUS;
class InputManager;
class Logging;
class MediaManager;
class Memory;
class PLA;
class SID;
class Vic;

struct CPUConfig;

class StateManager
{
    public:
        StateManager(Cartridge& cart,
                     Cassette& cass,
                     CIA1& cia1object,
                     CIA2& cia2object,
                     CPU& processor,
                     IECBUS& bus,
                     InputManager& inputMgr,
                     Logging& logger,
                     MediaManager& media,
                     Memory& mem,
                     PLA& pla,
                     SID& sidchip,
                     Vic& vicII,
                     std::atomic<bool>& uiPaused,
                     VideoMode& videoMode,
                     const CPUConfig*& cpuCfg,
                     bool& pendingBusPrime,
                     bool& busPrimedAfterBoot,
                     std::array<std::unique_ptr<Drive>, 16>& drives);

        ~StateManager();

        bool save(const std::string& path);
        bool load(const std::string& path);

    protected:

    private:
        Cartridge& cart_;
        Cassette& cass_;
        CIA1& cia1object_;
        CIA2& cia2object_;
        CPU& processor_;
        IECBUS& bus_;
        InputManager& inputMgr_;
        Logging& logger_;
        MediaManager& media_;
        Memory& mem_;
        PLA& pla_;
        SID& sidchip_;
        Vic& vicII_;

        static constexpr uint32_t kStateVersion = 1; // Save State file version

        std::atomic<bool>& uiPaused_;

        VideoMode& videoMode_;
        const CPUConfig*& cpuCfg_;

        bool& pendingBusPrime_;
        bool& busPrimedAfterBoot_;

        std::array<std::unique_ptr<Drive>, 16>& drives_;
};

#endif // STATEMANAGER_H
