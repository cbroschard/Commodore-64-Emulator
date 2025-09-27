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
#include <condition_variable>
#include <thread>
#include <mutex>
#include "Cartridge.h"
#include "cassette.h"
#include "CIA1.h"
#include "CIA2.h"
#include "CPU.h"
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
#include "Vic.h"

// Forward declarations
class MLMonitor;

struct CPUState
{
    uint16_t PC;
    uint8_t  A, X, Y, SP, SR;
};

// CPU timing for NTSC vs PAL
struct CPUConfig {
    double clockSpeedHz;   // master clock
    double frameRate;      // frames/sec

    // compute on-the-fly so it can never get out of sync
    constexpr int cyclesPerFrame() const {
        return int(clockSpeedHz / frameRate + 0.5);
    }
};

static constexpr CPUConfig NTSC_CPU = {1'022'727.0, 59.826};
static constexpr CPUConfig PAL_CPU  = {985'248.0, 50.125};

class Computer
{
    public:
        Computer();
        virtual ~Computer();

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

        // Game controls
        void setJoystickConfig(int port, JoystickMapping& cfg);

        // Setter for Video Mode
        void setVideoMode(const std::string& mode);

        // Setters for C64 ROM locations
        inline void setKernalROM(const std::string& kernal) { KERNAL_ROM = kernal; }
        inline void setBASIC_ROM(const std::string& basic) { BASIC_ROM = basic; }
        inline void setCHAR_ROM(const std::string& character) { CHAR_ROM = character; }

        // Setters for 1541 ROM locations
        inline void set1541LoROM(const std::string& loROM) { D1541LoROM = loROM; }
        inline void set1541HiROM(const std::string& hiROM) { D1541HiROM = hiROM; }

        // ML Monitor Cartridge methods
        inline Cartridge* getCart() { return cart.get(); }
        inline void detachCartridge() { cartridgeAttached = false; }
        inline bool getCartridgeAttached() { return cartridgeAttached; }

        // ML Monitor CIA1 Register Dumps
        inline std::string dumpCIA1Regs() const { return cia1object ? cia1object->dumpRegisters("all") : "CIA1 not attached\n"; }
        inline std::string dumpCIA1Ports() const { return cia1object ? cia1object->dumpRegisters("port") : "CIA1 not attached\n"; }
        inline std::string dumpCIA1Timers() const { return cia1object ? cia1object->dumpRegisters("timer") : "CIA1 not attached\n"; }
        inline std::string dumpCIA1TOD() const { return cia1object ? cia1object->dumpRegisters("tod") : "CIA1 not attached\n"; }
        inline std::string dumpCIA1ICR() const { return cia1object ? cia1object->dumpRegisters("icr") : "CIA1 not attached\n"; }
        inline std::string dumpCIA1Serial() const { return cia1object ? cia1object->dumpRegisters("serial") : "CIA1 not attached\n"; }
        inline std::string dumpCIA1Mode() const { return cia1object ? cia1object->dumpRegisters("mode") : "CIA1 not attached\n"; }

        // ML Monitor CIA2 Register Dumps
        inline std::string dumpCIA2Regs() const { return cia2object ? cia2object->dumpRegisters("all") : "CIA2 not attached\n"; }
        inline std::string dumpCIA2Ports() const { return cia2object ? cia2object->dumpRegisters("port") : "CIA2 not attached\n"; }
        inline std::string dumpCIA2Timers() const { return cia2object ? cia2object->dumpRegisters("timer") : "CIA2 not attached\n"; }
        inline std::string dumpCIA2TOD() const { return cia2object ? cia2object->dumpRegisters("tod") : "CIA2 not attached\n"; }
        inline std::string dumpCIA2ICR() const { return cia2object ? cia2object->dumpRegisters("icr") : "CIA2 not attached\n"; }
        inline std::string dumpCIA2Serial() const { return cia2object ? cia2object->dumpRegisters("serial") : "CIA2 not attached\n"; }
        inline std::string dumpCIA2VICBanks() const { return cia2object ? cia2object->dumpRegisters("vic") : "CIA2 not attached\n"; }
        inline std::string dumpCIA2IEC() const { return cia2object ? cia2object->dumpRegisters("iec") : "CIA2 not attached\n"; }

        // ML Monitor Cassette methods
        inline std::string dumpTapeDebug(size_t count) const { return cass ? cass->dumpPulses(count) : "CASSETTE not attached\n"; }

        // ML Monitor CPU methods
        inline uint16_t getPC() { return processor->getPC(); }
        inline void setPC(uint16_t address) { processor->setPC(address); }
        inline void cpuStep() { return processor->step(); }
        inline uint8_t getOpCode(uint16_t PC) { return mem->read(PC); }
        inline CPUState getCPUState() const { return CPUState{ processor->getPC(), processor->getA(), processor->getX(),
                 processor->getY(), processor->getSP(), processor->getSR()}; }
        void setJamMode(const std::string& mode);
        std::string getJamMode() const { return processor ? jamModeToString() : "Processor not attached\n"; }

        // ML Monitor Memory methods
        inline Memory* getMem() { return mem.get(); }
        inline uint8_t readRAM(uint16_t address) { return mem->read(address); }
        inline void writeRAM(uint16_t address, uint8_t value) { mem->write(address, value); }
        inline void writeRAMDirect(uint16_t address, uint8_t value) { mem->writeDirect(address, value); }

        // ML Monitor PLA methods
        inline std::string plaGetState() { return pla ? pla->describeMode() : "PLA not attached\n"; }
        inline std::string plaGetAddressInfo(uint16_t address) { return pla ? pla->describeAddress(address) : "PLA not attached\n"; }

        // ML Monitor SID Register Dumps
        inline std::string dumpSIDRegs() const { return sidchip ? sidchip->dumpRegisters("all") : "SID not attached\n"; }
        inline std::string dumpSIDVoice1() const { return sidchip ? sidchip->dumpRegisters("voice1") : "SID not attached\n"; }
        inline std::string dumpSIDVoice2() const { return sidchip ? sidchip->dumpRegisters("voice2") : "SID not attached\n"; }
        inline std::string dumpSIDVoice3() const { return sidchip ? sidchip->dumpRegisters("voice3") : "SID not attached\n"; }
        inline std::string dumpSIDVoices() const { return sidchip ? sidchip->dumpRegisters("voices") : "SID not attached\n"; }
        inline std::string dumpSIDFilter() const { return sidchip ? sidchip->dumpRegisters("filter") : "SID not attached\n"; }

        // ML Monitor VIC-II methods
        inline std::string vicGetModeName() { return vicII ? vicII->decodeModeName() : "VIC not attached\n"; }
        inline std::string getCurrentVICBanks() { return vicII ? vicII->getVICBanks() : "VIC not attached\n"; }
        inline std::string vicDumpRegs(const std::string& group) { return vicII ? vicII->dumpRegisters(group) : " VIC not attached\n"; }
        inline uint8_t getCurrentRaster() { return vicII->getCurrentRaster(); }
        void vicFFRaster(uint8_t targetRaster);

    protected:

    private:

        // Pointers
        std::unique_ptr<Cartridge> cart;
        std::unique_ptr<Cassette> cass;
        std::unique_ptr<CIA1> cia1object;
        std::unique_ptr<CIA2> cia2object;
        std::unique_ptr<CPU> processor;
        std::unique_ptr<IECBUS> bus;
        std::unique_ptr<IO> IO_adapter;
        std::unique_ptr<IRQLine> IRQ;
        std::unique_ptr<Joystick> joy1;
        std::unique_ptr<Joystick> joy2;
        std::unique_ptr<Keyboard> keyb;
        std::unique_ptr<Logging> logger;
        std::unique_ptr<Memory> mem;
        std::unique_ptr<MLMonitor> monitor;
        std::unique_ptr<PLA> pla;
        std::unique_ptr<SID> sidchip;
        std::unique_ptr<Vic> vicII;

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

        // Filenames and path of the ROMS to boot the system and 1541
        std::string KERNAL_ROM;
        std::string BASIC_ROM;
        std::string CHAR_ROM;
        std::string D1541LoROM;
        std::string D1541HiROM;

        // debugging
        bool isBASICReady();
        void debugBasicState();

        // Helper for Jam Modes
        std::string jamModeToString() const;
};

#endif // COMPUTER_H
