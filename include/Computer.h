// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef COMPUTER_H
#define COMPUTER_H

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <mutex>
#include <thread>
#include "Cartridge.h"
#include "cassette.h"
#include "CIA1.h"
#include "CIA2.h"
#include "CPU.h"
#include "Debug/MLMonitorBackend.h"
#include "Drive/D1571.h"
#include "IECBUS.h"
#include "IO.h"
#include "IRQLine.h"
#include "Joystick.h"
#include "keyboard.h"
#include "Logging.h"
#include "Memory.h"
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

        // Helper for screen printing
        void printWithChrout(const std::string& text);

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
        Joystick* getJoy1();
        Joystick* getJoy2();

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

    protected:

    private:

        // Pointers
        std::unique_ptr<Cartridge> cart;
        std::unique_ptr<Cassette> cass;
        std::unique_ptr<CIA1> cia1object;
        std::unique_ptr<CIA2> cia2object;
        std::unique_ptr<CPU> processor;
        std::unique_ptr<D1571> drive8;
        std::unique_ptr<IECBUS> bus;
        std::unique_ptr<IRQLine> IRQ;
        std::unique_ptr<Joystick> joy1;
        std::unique_ptr<Joystick> joy2;
        std::unique_ptr<Keyboard> keyb;
        std::unique_ptr<Logging> logger;
        std::unique_ptr<Memory> mem;
        std::unique_ptr<MLMonitor> monitor;
        std::unique_ptr<MLMonitorBackend> monbackend;
        std::unique_ptr<PLA> pla;
        std::unique_ptr<SID> sidchip;
        std::unique_ptr<IO> IO_adapter;
        std::unique_ptr<TraceManager> traceMgr;
        std::unique_ptr<Vic> vicII;

        // Imgui monitor toggle
        bool showMonitorOverlay;

        // Program loading delay counter
        int prgDelay;

        // Event handling
        bool handleInputEvent(const SDL_Event& ev);

        // Joystick
        void setJoystickAttached(int port, bool flag);
        JoystickMapping joy1Config;
        JoystickMapping joy2Config;
        std::unordered_map<SDL_Scancode, Joystick::direction> joyMap[3];

        // Video/CPU mode setup
        VideoMode videoMode_ = VideoMode::NTSC;
        const CPUConfig* cpuCfg_ = &NTSC_CPU;

        // Cartridge detection
        bool cartridgeAttached;
        std::string cartridgePath;

        // Tape image
        bool tapeAttached;
        std::string tapePath;
        bool t64Injected; // keep track of whether or not we loaded into memory direct for t64 image

        // Program file
        bool prgAttached;
        bool prgLoaded;
        std::string prgPath;
        bool loadPrgImage();
        void loadPrgIntoMem();
        std::vector<uint8_t> prgImage;

        // Disk image
        bool diskAttached;
        std::string diskPath;

        // Game controls
        bool joystick1Attached;
        bool joystick2Attached;
        bool checkCombo(SDL_Keymod modMask, SDL_Scancode a, SDL_Scancode b);

        // Graphics loop threading
        std::mutex              frameMut;
        std::condition_variable frameCond;
        std::atomic<bool>       frameReady;
        std::atomic<bool>       running;

        // Filenames and path of the ROMS to boot the system and Drive ROMS
        std::string KERNAL_ROM;
        std::string BASIC_ROM;
        std::string CHAR_ROM;
        std::string D1541LoROM;
        std::string D1541HiROM;
        std::string D1571ROM;

        // Build the ImGui Menu
        enum class CassCmd : uint8_t { None, Play, Stop, Rewind, Eject };
        std::atomic<int> uiVideoModeReq; // -1 = none, 0 = ntsc, 1 = pal
        std::atomic<bool> uiAttachPRG;
        std::atomic<bool> uiAttachCRT;
        std::atomic<bool> uiAttachT64;
        std::atomic<bool> uiAttachTAP;
        std::atomic<bool> uiToggleJoy1Req;
        std::atomic<bool> uiToggleJoy2Req;
        std::atomic<bool> uiQuit;
        std::atomic<bool> uiWarmReset;
        std::atomic<bool> uiColdReset;
        std::atomic<bool> uiPaused;
        std::atomic<bool> uiEnterMonitor;
        std::atomic<CassCmd> uiCass{CassCmd::None};
        void installMenu();

        // Menu helpers
        void attachPRGImage();
        void attachCRTImage();
        void attachT64Image();
        void attachTAPImage();
        void drawFileDialog();
        void startFileDialog(const std::string& title, std::vector<std::string> exts, std::function<void(const std::string&)> onAccept);

        struct FileDialogState
        {
            bool open;

            std::filesystem::path currentDir;
            std::string selectedEntry;
            std::string error;

            // Configuration for the current use
            std::string title;
            std::vector<std::string> allowedExtensions;
            std::function<void(const std::string&)> onAccept;
        };

        FileDialogState fileDlg;

        // debugging
        bool isBASICReady();
        void debugBasicState();
};

#endif // COMPUTER_H
