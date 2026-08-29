// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef MEMORY_H
#define MEMORY_H

// Forward declarations
#include <bitset>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include "Common/CartridgeTypes.h"
#include "StateReader.h"
#include "StateWriter.h"

class Memory
{
    public:

        Memory();
        virtual ~Memory();

        // State management
        void saveState(StateWriter& wrtr) const;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr);

        // Public access to memory
        uint8_t readRAM(uint16_t address) const;
        uint8_t readKernalROM(uint16_t address) const;
        uint8_t readBASICROM(uint16_t address) const;
        uint8_t readCharROM(uint16_t address) const;
        uint8_t readColorRAM(uint16_t address) const;

        void write16(uint16_t address, uint16_t value);
        void writeRAM(uint16_t address, uint8_t value);
        void writeColorRAM(uint16_t address, uint8_t value);
        void writeDirect(uint16_t address, uint8_t value);

        // Cartridge API
        uint8_t readCartridge(uint16_t address, cartLocation location) const;
        void writeCartridge(uint16_t address, uint8_t value, cartLocation location);

        // Load all ROMS
        bool Initialize(const std::string& basic, const std::string& kernal, const std::string& character);

        // Helpers for certain cartridge types
        inline uint8_t getCartLOByte(uint16_t offset) const { return (offset < cart_lo.size()) ? cart_lo[offset] : 0xFF; }
        inline uint8_t getCartHIByte(uint16_t offset) const { return (offset < cart_hi.size()) ? cart_hi[offset] : 0xFF; }

    private:
        // RAM/ROM
        std::vector<uint8_t> mem;
        std::vector<uint8_t> basicROM;
        std::vector<uint8_t> charROM;
        std::vector<uint8_t> kernalROM;
        std::vector<uint8_t> colorRAM;
        std::vector<uint8_t> cart_lo;
        std::vector<uint8_t> cart_hi;
        std::vector<uint8_t> cart_hi_e000;

        // Rom constants
        static constexpr size_t BASIC_ROM_SIZE      = 0x2000;
        static constexpr size_t KERNAL_ROM_SIZE     = 0x2000;
        static constexpr size_t CHAR_ROM_SIZE       = 0x1000;
        static constexpr size_t CART_LO_SIZE        = 0x2000;
        static constexpr size_t CART_HI_SIZE        = 0x2000;
        static constexpr size_t CART_HI_E000_SIZE   = 0x2000;
        static constexpr size_t MAX_MEMORY          = 0x10000;
        static constexpr size_t COLOR_RAM_SIZE      = 0x400;
        static const uint16_t COLOR_MEMORY_START    = 0xD800;
        static const uint16_t COLOR_MEMORY_END      =  0xDBFF;

        bool load_ROM(const std::string& filename, std::vector<uint8_t>& targetBuffer, size_t expectedSize, const std::string& romName);
};

#endif // MEMORY_H
