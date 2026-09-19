// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef CARTRIDGE_H
#define CARTRIDGE_H

#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include "Cartridge/CartridgeMapper.h"
#include "Cartridge/ICartridgeHost.h"
#include "Common/CartridgeTypes.h"
#include "Common/Endian.h"
#include "StateReader.h"
#include "StateWriter.h"
#include "TraceManager.h"

// Forward declarations
class CPU;
class DataBusLatch;
class Vic;

class Cartridge
{
    public:
        Cartridge();
        virtual ~Cartridge();

        inline void attachBusInstance(Bus* bus) { this->bus = bus; }
        inline void attachCPUInstance(CPU* cpu) { this->cpu = cpu; }
        inline void attachDataBusLatchInstance(DataBusLatch* dataBus) { this->dataBus = dataBus; }
        inline void attachHostInstance(ICartridgeHost* host) { this->host = host; }
        inline void attachTraceManagerInstance(TraceManager* traceMgr) { this->traceMgr = traceMgr; }
        inline void attachVicInstance(Vic* vic) { this->vic = vic; }

        inline double getCPUClockHz() const { return host ? host->getCPUClockHz() : 0.0; }

        // State management
        void saveState(StateWriter& wrtr) const;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr);

        void reset();
        void clear();

        // Host interface
        void requestWarmReset();
        void requestColdReset();
        void requestCartridgeNMI();

        bool loadROM(const std::string& path);  // load the Cartridge

        bool setCurrentBank(uint16_t bank);      // Update the current bank for loading

        // Live cartridge pin *levels* (1=inactive/high, 0=asserted/low)
        inline bool getExROMLine() const { return exROMLine; }
        inline bool getGameLine()  const { return gameLine; }

        void setExROMLine(bool level);
        void setGameLine(bool level);

        // Open Bus sample
        uint8_t sampleDataBus() const;

        std::string getGameName() const;

        // Public read/write access to cartridge memory
        uint8_t read(uint16_t address);
        uint8_t readRAM(size_t offset);
        uint8_t peek(uint16_t ddress) const;
        uint8_t peekRAM(size_t offset) const;
        void write(uint16_t address, uint8_t value);
        void writeRAM(size_t offset, uint8_t value);
        uint8_t readCartridge(uint16_t offset, cartLocation location) const;
        void writeCartridge(uint16_t address, uint8_t value, cartLocation location);

        inline size_t ramSize() const { return ramData.size(); }
        inline bool hasCartridgeRAM() const { return hasRAM && !ramData.empty(); }


        struct chipSection
        {
            uint8_t chipType;               // Same as in crtChipHeader
            uint16_t bankNumber;            // Bank number of 16k section
            uint16_t loadAddress;           // Same as in crtChipHeader
            std::vector<uint8_t> data;      // CHIP section data to load
        };

        // Define the wiring types for cartridges
        enum class WiringMode
        {
            NONE,
            CART_8K,
            CART_16K,
            CART_ULTIMAX
        };

        // Map various cartridge types per VICE docs
        enum class CartridgeType
        {
            GENERIC,                // 0
            ACTION_REPLAY,          // 1
            KCS_POWER,              // 2
            FINAL_CARTRIDGE_III,    // 3
            SIMONS_BASIC,           // 4
            OCEAN,                  // 5
            EXPERT,                 // 6
            FUN_PLAY,               // 7
            SUPER_GAMES,            // 8
            ATOMIC_POWER,           // 9
            EPYX_FASTLOAD,          // 10
            WESTERMANN,             // 11
            REX_UTILITY,            // 12
            FINAL_CARTRIDGE,        // 13
            MAGIC_FORMEL,           // 14
            C64_GAME_SYSTEM,        // 15
            WARP_SPEED,             // 16
            DINAMIC,                // 17
            SUPER_ZAXXON,           // 18
            MAGICDESK,              // 19
            SUPER_SNAPSHOT_V5,      // 20
            COMAL_80,               // 21
            STRUCTURED_BASIC,       // 22
            ROSS,                   // 23
            DELA_EP64,              // 24
            DELA_EP7X8,             // 25
            DELA_EP256,             // 26
            REX_EP256,              // 27
            MIKRO_ASSEMBLER,        // 28
            FINAL_CARTRIDGE_PLUS,   // 29
            ACTION_REPLAY_4,        // 30
            STARDOS,                // 31
            EASYFLASH,              // 32

            // 33 EasyFlash Xbank not supported

            CAPTURE,                // 34
            ACTION_REPLAY_3,        // 35
            RETRO_REPLAY,           // 36

            // 37 MMC64
            // 38 MMC Replay

            IDE64,                  // 39
            SUPER_SNAPSHOT_V4,      // 40

            // 41 IEEE-488
            // 42 Game Killer
            PROPHET_64,             // 43
            EXOS,                   // 44

            FREEZE_FRAME,           // 45
            FREEZE_MACHINE,         // 46
            SNAPSHOT_64,            // 47

            SUPER_EXPLODE_V5,       // 48
            // 49 Magic Voice

            ACTION_REPLAY_2,        // 50
            MACH_5,                 // 51
            DIASHOW_MAKER,          // 52

            PAGEFOX,                // 53
            KINGSOFT,               // 54
            SILVERROCK_128,         // 55
            // 56 Formel 64

            RGCD,                   // 57

            // 58 RR-Net MK3

            EASYCALC,               // 59
            GMOD2,                  // 60
            MAX_BASIC,              // 61

            // 62 GMod3

            ZIPP_CODE_48,           // 63
            BLACKBOX_V8,            // 64
            BLACKBOX_V3,            // 65
            BLACKBOX_V4,            // 66

            // 67 REX RAM-Floppy
            BIS_PLUS,               // 68
            // 69 SD-BOX
            // 70 MultiMAX

            BLACKBOX_V9,            // 71

            // 72 Lt. Kernal Host Adaptor
            // 73 RAMLink
            DREAN,                  // 74
            // 75 IEEE Flash! 64
            TURTLE_GRAPHICE_II,     // 76

            FREEZE_FRAME_MK2,       // 77

            PARTNER_64,             // 78
            HYPER_BASIC,            // 79
            UNIVERSAL_CARTRIDGE_1,  // 80
            UNIVERSAL_CARTRIDGE_15, // 81
            UNIVERSAL_CARTRIDGE_2,   // 82
            // 83 BMP Data Turbo 2000
            // 84 Profi-DOS

            MAGICDESK_16,           // 85

            UNKNOWN
        };

        // Getters
        inline uint8_t getHardwareRevision() const { return header.revision; }
        inline CartridgeMapper* getMapper() { return mapper.get(); }
        inline const CartridgeMapper* getMapper() const { return mapper.get(); }

        // Setters
        void setExternalKernalActive(bool enabled);

        CartridgeType getType() const;
        std::string getMapperName() const;

        // Helpers
        inline uint16_t getCurrentBank() const { return currentBank; }
        uint16_t getNumberOfBanks() const;
        bool hasSectionAt(uint16_t address) const;

        // Clear cartridge memory
        void clearCartridge(cartLocation location);

        // Cartridge Mapper access
        inline const std::vector<chipSection>& getChipSections() const { return chipSections; }
        inline WiringMode getWiringMode() { return wiringMode; }
        inline size_t getCartridgeSize() const { return cartSize / 1024; }
        inline std::vector<chipSection>& getChipSections() { return chipSections; }

        // EEPROM API
        inline bool romWriteEnabled(uint16_t address) const { return mapper ? mapper->romWriteEnabled(address) : false; }
        inline bool romReadHandledByMapper(uint16_t address) const { return mapper ? mapper->romReadHandledByMapper(address) : false; }

        inline bool cpuReadHandledByMapper(uint16_t address) const { return mapper && mapper->cpuReadHandledByMapper(address); }
        CartridgeWriteRoute cpuWriteRoute(uint16_t address) const;

    protected:
        // Cartridge LO/HI location constants
        static constexpr size_t CART_LO_START = 0x8000;
        static constexpr size_t CART_HI_START = 0xA000;
        static constexpr size_t CART_HI_START1 = 0xE000;

        std::vector<chipSection> chipSections;  // vector for ROM chip banks
        std::vector<uint8_t> romData;           // vector to store the Cartridge rom
        std::vector<uint8_t> ramData;           // vector for Cartridge ram if supported
        bool hasRAM;                            // Set for Cartridges that have RAM
        uint16_t currentBank;                    // Support bank switching

    private:
        // Non-owning pointers
        Bus* bus;
        CPU* cpu;
        DataBusLatch* dataBus;
        ICartridgeHost* host;
        TraceManager* traceMgr;
        Vic* vic;

        // Polymorphic pointer for cartridge mapper types
        std::unique_ptr<CartridgeMapper> mapper;

        // Stroage
        std::vector<uint8_t> cart_lo;
        std::vector<uint8_t> cart_hi;
        std::vector<uint8_t> cart_hi_e000;

        static constexpr size_t CART_LO_SIZE        = 0x2000;
        static constexpr size_t CART_HI_SIZE        = 0x2000;
        static constexpr size_t CART_HI_E000_SIZE   = 0x2000;

        // Wiring mode
        WiringMode wiringMode;

        // Keep track of cartridge size
        size_t cartSize;

        // Line levels
        bool exROMLine;
        bool gameLine;

        // Cartridge mapping
        CartridgeType mapperType;
        CartridgeType detectType(uint16_t hardwareType);

        // EEPROM persistence
        std::string persistencePath;

        // Loaders
        bool loadFile(const std::string& path, std::vector<uint8_t>& buffer);
        bool loadIntoMemory();

        // Helper functions
        bool processChipSections();
        void determineWiringMode();

        #pragma pack(push,1)
        struct crtHeader
        {
            char magic[16];                  // Magic Header should say C64 CARTRIDGE
            uint32_t headerLength;           // File header length in high/low format
            uint16_t CartridgeVersion;       // Cartridge version high/low format
            uint16_t CartridgeHardwareType;  // Cartridge hardware type in high/low format
            uint8_t exROMLine;               // Helps determine type of Cartridge (8k,16K,ultimax)
            uint8_t gameLine;                // Helps determine type of Cartridge (8k,16K,ultimax)
            uint8_t revision;                // Should be 0
            uint8_t reserved[5];             // Reserved and not currently used
            char gameName[32];               // Name of the game
        } header;
        #pragma pack(pop)

        #pragma pack(push,1)
        struct crtChipHeader
        {
            char signature[4];               // Should read as CHIP
            uint32_t packetLength;           // Length of ROM image size and header combined
            uint16_t chipType;               //  0 - ROM, 1 - RAM, 2 - Flash ROM, 3 - EEPROM
            uint16_t bankNumber;             // Number of the bank this CHIP is in
            uint16_t loadAddress;            // Used to tell the loader which part of the given bank is to be used for this chunk
            uint16_t romSize;                // The size of the ROM image in bytes
        };
        #pragma pack(pop)

        // Cartridge type specific helpers
        inline uint8_t decodeFunPlayBank(uint8_t value) { return ((value & 0x38) >> 3) | ((value & 0x01) << 3); }

        // Tracing helper
        void traceActiveWindows(const char* why);

        static uint16_t selectInitialBank(const std::vector<Cartridge::chipSection>& sections);
        bool mapCpuAddrToCartOffset(uint16_t cpuAddr, Cartridge::WiringMode wiringMode, cartLocation& outLoc, uint16_t& outOffset);
        std::unique_ptr<CartridgeMapper> createMapper(CartridgeType t);

        // Cart RAM helpers
        void configureRAM(size_t bytes);
        void clearRAM();

        // EEPROM helpers
        std::string makePersistencePath(const std::string& romPath) const;
        void saveCurrentPersistence();

        TraceManager::Stamp makeCartStamp() const;
};

#endif // CARTRIDGE_H
