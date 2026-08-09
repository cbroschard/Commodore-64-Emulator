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
class DataBusLatch;
class Drive;
class IECBUS;
class InputManager;
class MediaManager;
class Memory;
class PLA;
class RS232Device;
class SID;
class UserPortRS232Adapter;
class Vic;

struct CPUConfig;

class StateManager
{
    public:
        StateManager(Cartridge& cart,
                     Cassette& cass,
                     CIA1& cia1,
                     CIA2& cia2,
                     CPU& processor,
                     DataBusLatch& dataBus,
                     IECBUS& bus,
                     InputManager& inputMgr,
                     MediaManager& media,
                     Memory& mem,
                     PLA& pla,
                     REU& reu,
                     RS232Device& rs232Device,
                     SID& sidchip,
                     UserPortRS232Adapter& userPortRS232Adapter,
                     Vic& vicII,
                     std::atomic<bool>& uiPaused,
                     VideoMode& videoMode,
                     SIDModel& sidModel,
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
        CIA1& cia1_;
        CIA2& cia2_;
        CPU& processor_;
        DataBusLatch& dataBus_;
        IECBUS& bus_;
        InputManager& inputMgr_;
        MediaManager& media_;
        Memory& mem_;
        PLA& pla_;
        REU& reu_;
        RS232Device& rs232Device_;
        SID& sidchip_;
        UserPortRS232Adapter& userPortRS232Adapter_;
        Vic& vicII_;

        static constexpr uint32_t kStateVersion = 1; // Save State file version

        std::atomic<bool>& uiPaused_;

        VideoMode& videoMode_;
        SIDModel& sidModel_;
        const CPUConfig*& cpuCfg_;

        bool& pendingBusPrime_;
        bool& busPrimedAfterBoot_;

        std::array<std::unique_ptr<Drive>, 16>& drives_;
};

#endif // STATEMANAGER_H
