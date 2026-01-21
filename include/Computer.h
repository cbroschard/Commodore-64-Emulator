// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef COMPUTER_H
#define COMPUTER_H

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "Cartridge.h"
#include "cassette.h"
#include "CIA1.h"
#include "CIA2.h"
#include "CPU.h"
#include "Debug/MLMonitorBackend.h"
#include "Drive/D1541.h"
#include "Drive/D1571.h"
#include "Drive/D1581.h"
#include "Drive/Drive.h"
#include "EmulatorUI.h"
#include "IECBUS.h"
#include "IO.h"
#include "InputManager.h"
#include "IRQLine.h"
#include "Joystick.h"
#include "keyboard.h"
#include "Logging.h"
#include "MediaManager.h"
#include "Memory.h"
#include "MonitorController.h"
#include "PLA.h"
#include "SID/SID.h"
#include "Tape/TapeImageFactory.h"
#include "Debug/TraceManager.h"
#include "Vic.h"

// Forward declarations
class MLMonitor;

class Computer
{
    public:
        Computer();
        virtual ~Computer();

        // CPU timing for NTSC vs PAL
        struct CPUConfig
        {
            double clockSpeedHz;   // master clock
            double frameRate;      // frames/sec

            // compute on-the-fly so it can never get out of sync
            constexpr int cyclesPerFrame() const
            {
                return int(clockSpeedHz / frameRate + 0.5);
            }
        };

        static constexpr CPUConfig NTSC_CPU = {1'022'727.0, 59.826};
        static constexpr CPUConfig PAL_CPU  = {985'248.0, 50.125};

        // Main emulation loop
        bool boot();

        // Reset methods
        void warmReset();
        void coldReset();

        // Attachments
        inline void setCartridgeAttached(bool flag) { cartridgeAttached = flag; }
        inline void setCartridgePath(const std::string& path) { cartridgePath = path; }
        inline void setTapeAttached(bool flag) { tapeAttached = flag; }
        inline void setTapePath(const std::string& path) { tapePath = path; }
        inline void setPrgAttached(bool flag) { prgAttached = flag; }
        inline void setPrgPath(const std::string& path) { prgPath = path; }
        inline void setDiskAttached(bool flag) { diskAttached = flag; }
        inline void setDiskPath(const std::string& path) { diskPath = path; }

        // Getters
        inline bool getCartridgeAttached() { return cartridgeAttached; }
        inline Joystick* getJoy1() { return input ? input->getJoy1() : nullptr; }
        inline Joystick* getJoy2() { return input ? input->getJoy2() : nullptr; }

        // Game controls
        void setJoystickConfig(int port, JoystickMapping& cfg);

        // Setter for Video Mode
        void setVideoMode(const std::string& mode);

        // Setters for C64 ROM locations
        inline void setKernalROM(const std::string& kernal) { KERNAL_ROM = kernal; }
        inline void setBASIC_ROM(const std::string& basic) { BASIC_ROM = basic; }
        inline void setCHAR_ROM(const std::string& character) { CHAR_ROM = character; }

        // Setters for Drive model ROM locations
        inline void set1541LoROM(const std::string& loROM) { D1541LoROM = loROM; }
        inline void set1541HiROM(const std::string& hiROM) { D1541HiROM = hiROM; }
        inline void set1571ROM(const std::string& rom) { D1571ROM = rom; }

        // ML Monitor
        void enterMonitor();

    protected:

    private:

        // Pointers
        std::unique_ptr<Cartridge> cart;
        std::unique_ptr<Cassette> cass;
        std::unique_ptr<CIA1> cia1object;
        std::unique_ptr<CIA2> cia2object;
        std::unique_ptr<CPU> processor;
        std::unique_ptr<D1541> drive8;
        std::array<std::unique_ptr<Drive>, 16> drives;
        std::unique_ptr<EmulatorUI> ui;
        std::unique_ptr<IECBUS> bus;
        std::unique_ptr<InputManager> input;
        std::unique_ptr<IRQLine> IRQ;
        std::unique_ptr<Keyboard> keyb;
        std::unique_ptr<Logging> logger;
        std::unique_ptr<MediaManager> media;
        std::unique_ptr<Memory> mem;
        std::unique_ptr<MLMonitor> monitor;
        std::unique_ptr<MLMonitorBackend> monbackend;
        std::unique_ptr<MonitorController> monitorCtl;
        std::unique_ptr<PLA> pla;
        std::unique_ptr<SID> sidchip;
        std::unique_ptr<IO> IO_adapter;
        std::unique_ptr<TraceManager> traceMgr;
        std::unique_ptr<Vic> vicII;

        // Program loading delay counter
        int prgDelay;

        // Event handling
        bool handleInputEvent(const SDL_Event& ev);

        // Joystick
        void setJoystickAttached(int port, bool flag);

        // Video/CPU mode setup
        VideoMode videoMode_ = VideoMode::NTSC;
        const CPUConfig* cpuCfg_ = &NTSC_CPU;

        // Cartridge detection
        bool cartridgeAttached;
        std::string cartridgePath;

        // Tape image
        bool tapeAttached;
        std::string tapePath;

        // PRG file loading
        bool prgAttached;
        bool prgLoaded;
        std::string prgPath;
        bool loadPrgImage();
        void loadPrgIntoMem();
        std::vector<uint8_t> prgImage;

        // Disk image
        bool diskAttached;
        std::string diskPath;

        // Graphics loop threading
        std::atomic<bool>       running;

        // Filenames and path of the ROMS to boot the system and Drive ROMS
        std::string KERNAL_ROM;
        std::string BASIC_ROM;
        std::string CHAR_ROM;
        std::string D1541LoROM;
        std::string D1541HiROM;
        std::string D1571ROM;

        std::atomic<bool> uiQuit;
        std::atomic<bool> uiPaused;

        // Bus priming
        bool pendingBusPrime;
        bool busPrimedAfterBoot;

        // helpers
        std::string lowerExt(const std::string& path);
        bool isExtCompatible(UiCommand::DriveType driveType, const std::string& ext);
        void attachDiskImage(int deviceNum, UiCommand::DriveType driveType, const std::string& path);
        void attachD64Image();
        void attachPRGImage();
        void attachCRTImage();
        void attachT64Image();
        void attachTAPImage();

        // Cartridge helper for attaching new cartridge over top of existing one
        void recreateCartridge();

        // Menu integration
        EmulatorUI::MediaViewState buildUIState() const;
        void processUICommands();

        // Wire all the components together
        void wireUp();
};

#endif // COMPUTER_H
