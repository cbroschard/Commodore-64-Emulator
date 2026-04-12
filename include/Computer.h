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
#include <cstdint>
#include <memory>
#include <string>
#include "Cartridge/ICartridgeHost.h"
#include "CPUTiming.h"
#include "MachineComponents.h"

// Forward declarations
class DebugManager;
class MLMonitor;
class ResetController;
class StateManager;
class UIBridge;

class Computer : public ICartridgeHost
{
    public:
        Computer();
        ~Computer() noexcept;

        // State Management
        bool saveStateToFile(const std::string& path);
        bool loadStateFromFile(const std::string& path);

        // Cartridge Host Interface
        void requestWarmReset() override;
        void requestColdReset() override;
        void requestCartridgeNMI() override;

        // Main emulation loop
        bool boot();

        // Reset methods
        void warmReset();
        void coldReset();

        // Setter for video mode
        void setVideoMode(const std::string& mode);

        // Attachments
        inline void setCartridgeAttached(bool flag) { if (components_.media) components_.media->setCartAttached(flag); }
        inline void setCartridgePath(const std::string& path) { if (components_.media) components_.media->setCartPath(path); }
        inline void setTapeAttached(bool flag) { if (components_.media) components_.media->setTapeAttached(flag); }
        inline void setTapePath(const std::string& path) { if (components_.media) components_.media->setTapePath(path); }
        inline void setPrgAttached(bool flag) { if (components_.media) components_.media->setPrgAttached(flag); }
        inline void setPrgPath(const std::string& path) { if (components_.media) components_.media->setPrgPath(path); }

        // Getters
        inline bool getCartridgeAttached() { return components_.media ? components_.media->getState().cartAttached : false; }
        inline Joystick* getJoy1() { return components_.inputMgr ? components_.inputMgr->getJoy1() : nullptr; }
        inline Joystick* getJoy2() { return components_.inputMgr ? components_.inputMgr->getJoy2() : nullptr; }

        // Game controls
        void setJoystickConfig(int port, JoystickMapping& cfg);

        // Setters for C64 ROM locations
        inline void setKernalROM(const std::string& kernal) { KERNAL_ROM = kernal; }
        inline void setBASIC_ROM(const std::string& basic) { BASIC_ROM = basic; }
        inline void setCHAR_ROM(const std::string& character) { CHAR_ROM = character; }

        // Setters for Drive model ROM locations
        void set1541LoROM(const std::string& loROM);
        void set1541HiROM(const std::string& hiROM);
        void set1571ROM(const std::string& rom);
        void set1581ROM(const std::string& rom);

        // ML Monitor
        void enterMonitor();

    protected:

    private:

        // Machine components
        MachineComponents components_;

        // Joystick
        void setJoystickAttached(int port, bool flag);

        // Video/CPU mode setup
        VideoMode videoMode_ = VideoMode::NTSC;
        const CPUConfig* cpuCfg_ = &NTSC_CPU;

        // Graphics loop threading
        std::atomic<bool>       running;

        // Filenames and path of the ROMS to boot the system and Drive ROMS
        std::string KERNAL_ROM;
        std::string BASIC_ROM;
        std::string CHAR_ROM;
        std::string D1541LoROM;
        std::string D1541HiROM;
        std::string D1571ROM;
        std::string D1581ROM;

        std::atomic<bool> uiQuit;
        std::atomic<bool> uiPaused;

        // Bus priming
        bool pendingBusPrime;
        bool busPrimedAfterBoot;

        // Wire all the components together
        void wireUp();
};

#endif // COMPUTER_H
