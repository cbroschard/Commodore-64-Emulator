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
#include <set>
#include <string>
#include <vector>
#include "Cartridge/CartridgeMapper.h"
#include "Memory.h"
#include "cpu.h"
#include "Logging.h"
#include "common.h"

class Cartridge
{
    public:
        Cartridge();
        virtual ~Cartridge();

        inline void attachMemoryInstance(Memory* mem) { this->mem = mem; }
        inline void attachLogInstance(Logging* logger) { this->logger = logger; }

        bool loadROM(const std::string& path); // load the Cartridge

        bool setCurrentBank(uint8_t bank); // Update the current bank for loading

        // Getters to help determine how to set memory control register
        inline bool getExROMLine() const { return header.exROMLine; }
        inline bool getGameLine() const { return header.gameLine; }

        // Getter for game name
        inline std::string getGameName() const { return std::string(header.gameName); }

        // Public read/write access to cartridge memory
        uint8_t read(uint16_t address);
        void write(uint16_t address, uint8_t value);

        struct chipSection
        {
            uint8_t chipType;               // Same as in crtChipHeader
            uint8_t bankNumber;            // Bank number of 16k section
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
            GENERIC,
            ACTION_REPLAY,
            KCS_POWER,
            FINAL_CARTRIDGE_III,
            SIMONS_BASIC,
            OCEAN,
            C64_GAME_SYSTEM,
            DINAMIC,
            SUPER_ZAXXON,
            MAGICDESK,
            EASYFLASH,
            WESTERMANN,
            FUN_PLAY,
            SUPER_GAMES,
            STRUCTURED_BASIC,
            ROSS,
            UNKNOWN
        };

        // Type getters
        CartridgeType getType() const;
        std::string getMapperName() const;

        // Helpers
        inline uint16_t getCurrentBank() const { return currentBank; }
        uint16_t getNumberOfBanks() const;
        bool hasSectionAt(uint16_t address) const;

        // Clear cartridge memory
        void clearCartridge(cartLocation location);

        // Getters/Setters for Cartridge Mappers
        inline const std::vector<chipSection>& getChipSections() const { return chipSections; }
        inline void setExROMLine(bool exROMLine) { header.exROMLine = exROMLine; }
        inline void setGameLine(bool gameLine) { header.gameLine = gameLine; }
        inline WiringMode getWiringMode() { return wiringMode; }
        inline size_t getCartridgeSize() const { return cartSize / 1024; }

    protected:

        // Cartridge LO/HI location constants
        static constexpr size_t CART_LO_START = 0x8000;
        static constexpr size_t CART_HI_START = 0xA000;
        static constexpr size_t CART_HI_START1 = 0xE000;

        std::vector<chipSection> chipSections; // vector for ROM chip banks
        std::vector<uint8_t> romData; // vector to store the Cartridge rom
        std::vector<uint8_t> ramData; // vector for Cartridge ram if supported
        bool hasRAM; // Set for Cartridges that have RAM
        uint8_t currentBank; // Support bank switching

    private:

        // Non-owning pointers
        Memory* mem = nullptr;
        Logging* logger = nullptr;

        // Polymorphic pointer for cartridge mapper types
        std::unique_ptr<CartridgeMapper> mapper;

        // Wiring mode
        WiringMode wiringMode;

        // Keep track of cartridge size
        size_t cartSize;

        // Cartridge mapping
        CartridgeType mapperType;
        CartridgeType detectType(uint16_t hardwareType);

        // Loaders
        bool loadFile(const std::string& path, std::vector<uint8_t>& buffer);
        bool loadIntoMemory();

        // Helper functions
        bool processChipSections();
        void determineWiringMode();

        #pragma pack(push,1)
        struct crtHeader
        {
            char magic[16];                 // Magic Header should say C64 CARTRIDGE
            uint32_t headerLength;          // File header length in high/low format
            uint16_t CartridgeVersion;      // Cartridge version high/low format
            uint16_t CartridgeHardwareType;  // Cartridge hardware type in high/low format
            uint8_t exROMLine;              // Helps determine type of Cartridge (8k,16K,ultimax)
            uint8_t gameLine;               // Helps determine type of Cartridge (8k,16K,ultimax)
            uint8_t revision;               // Should be 0
            uint8_t reserved[5];               // Reserved and not currently used
            char gameName[32];              // Name of the game
        } header;
        #pragma pack(pop)

        #pragma pack(push,1)
        struct crtChipHeader
        {
            char signature[4];              // Should read as CHIP
            uint32_t packetLength;          // Length of ROM image size and header combined
            uint16_t chipType;               //  0 - ROM, 1 - RAM, 2 - Flash ROM, 3 - EEPROM
            uint16_t bankNumber;            // Number of the bank this CHIP is in
            uint16_t loadAddress;           // Used to tell the loader which part of the given bank is to be used for this chunk
            uint16_t romSize;               // The size of the ROM image in bytes
        };
        #pragma pack(pop)

        // Cartridge type specific helpers
        inline uint8_t decodeFunPlayBank(uint8_t value) { return ((value & 0x38) >> 3) | ((value & 0x01) << 3); }
};

#endif // CARTRIDGE_H
