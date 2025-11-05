// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef D1541MEMORY_H
#define D1541MEMORY_H

#include <algorithm>
#include <vector>
#include <cstdint>
#include <fstream>
#include <iostream>
#include "D1541VIA.h"
#include "Logging.h"
#include "Memory.h"
#include "IRQLine.h"

class D1541Memory : public Memory
{
    public:
        D1541Memory();
        virtual ~D1541Memory();

        // Pointers
        void attachLoggingInstance(Logging* logger);

        // Tick to advance the VIA chips
        void tick();

        // reset function
        void reset();

        // Interfaces to memory access
        uint8_t read(uint16_t address);
        void write(uint16_t address, uint8_t value);

        // Initialize function to load all ROMS passed in via config file, initialize RAM
        bool initialize(const std::string& D1541LoROM, const std::string& D1541HiROM);

        // Getters for access for D1541
        IRQLine* getIRQLine();
        D1541VIA& getVIA1();
        D1541VIA& getVIA2();

    protected:

    private:

        // Non-owning pointers
        Logging* logger = nullptr;

        // VIA objects
        D1541VIA via1;
        D1541VIA via2;
        IRQLine driveIRQ;

        // Memory vectors
        std::vector<uint8_t> D1541RAM;
        std::vector<uint8_t> D1541ROM1;
        std::vector<uint8_t> D1541ROM2;

        // RAM constants
        static constexpr size_t RAM_SIZE = 0x0800; // 2K RAM
        static const uint16_t D1541_RAM_START = 0x0000;
        static const uint16_t D1541_RAM_END = 0x07FF;

        // ROM constants
        static constexpr size_t ROM_SIZE = 0x2000; // 8K per ROM
        static const uint16_t ROM1_START = 0xC000;
        static const uint16_t ROM1_END  = 0xDFFF;
        static const uint16_t ROM2_START = 0xE000;
        static const uint16_t ROM2_END = 0xFFFF;

        // VIA Constants
        static const uint16_t VIA1_START = 0x1800;
        static const uint16_t VIA1_END = 0x180F;
        static const uint16_t VIA2_START = 0x1C00;
        static const uint16_t VIA2_END =  0x1C0F;

        // ROM Loader
        bool loadROM(const std::string& filename, std::vector<uint8_t>& targetBuffer, size_t expectedSize, const std::string& romName);
};

#endif // D1541MEMORY_H
