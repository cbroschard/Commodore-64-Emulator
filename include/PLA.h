// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef PLA_H
#define PLA_H

#include <cstdint>
#include <iostream>
#include "Cartridge.h"
#include "Logging.h"
#include "Vic.h"

class PLA
{
    public:
        PLA();
        virtual ~PLA();

        enum memoryBank
        {
            RAM,
            KERNAL_ROM,
            BASIC_ROM,
            CHARACTER_ROM,
            CARTRIDGE_LO,
            CARTRIDGE_HI,
            IO,
            UNMAPPED
        };

        struct memoryAccessInfo
        {
            memoryBank bank;
            uint16_t offset;
        };

        inline void attachCartridgeInstance(Cartridge* cart) { this->cart = cart; }
        inline void attachVICInstance(Vic* vicII) { this->vicII = vicII; }
        inline void attachLogInstance(Logging* logger) { this->logger = logger; }

        // Standard reset routine
        void reset();

        // Allow memory class to query PLA for current status
        memoryAccessInfo getMemoryAccess(uint16_t address);

        // Memory control register getters
        bool getLORAM() const { return loram; }
        bool getHIRAM() const { return hiram; }
        bool getCHAREN() const { return charen; }

        // Cartridge setter
        void setCartridgeAttached(bool flag) { cartridgeAttached = flag; }

        // Getters
        bool getExROMLine() const { return exROMLine; }
        bool getGameLine() const { return gameLine; }

        // Memory control register
        uint8_t getMemoryControlRegister() const { return memoryControlRegister; }
        void updateMemoryControlRegister(uint8_t value);

        // Cartridge attached types
        inline bool is8K() const { return  gameLine && !exROMLine; } // GAME=1, EXROM=0
        inline bool is16K() const { return !gameLine && !exROMLine; } // GAME=0, EXROM=0
        inline bool isUltimax()const { return !gameLine &&  exROMLine; } // GAME=0, EXROM=1

        // ML Monitor API
        std::string describeAddress(uint16_t addr);
        std::string describeMode();
        inline void setLog(bool enable) { setLogging = enable; }

    protected:

    private:

        // Non-owning pointers
        Cartridge* cart = nullptr;
        Logging* logger = nullptr;
        Vic* vicII = nullptr;

        // ROM constants
        static const uint16_t CHAR_ROM_START = 0xD000;
        static const uint16_t CHAR_ROM_END = 0xDFFF;

        // Memory lines
        bool loram;
        bool hiram;
        bool charen;

        // Cartridge lines
        bool exROMLine;
        bool gameLine;

        // Keep track of a cartridge "inserted"
        bool cartridgeAttached;

        // Memory control register $0001
        uint8_t memoryControlRegister;

        // ML Monitor logging
        bool setLogging;

        const char* bankToString(PLA::memoryBank bank);
};

#endif // PLA_H
