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
#include <cstdint>
#include <fstream>
#include <iostream>
#include <vector>
#include "Drive/D1541VIA.h"
#include "Drive/DriveChips.h"
#include "IRQLine.h"
#include "Logging.h"
#include "Peripheral.h"
#include "StateReader.h"
#include "StateWriter.h"

class D1541Memory : public DriveMemoryBase
{
    public:
        D1541Memory();
        virtual ~D1541Memory();

        // Pointers
        inline void attachLoggingInstance(Logging* logger) { this->logger = logger; }
        void attachPeripheralInstance(Peripheral* parentPeripheral);

        // Tick to advance the VIA chips
        void tick(uint32_t cycles);

        // State Management
        void saveState(StateWriter& wrtr) const;
        bool loadState(StateReader& rdr);

        // reset function
        void reset();

        // Interfaces to memory access
        uint8_t read(uint16_t address);
        void write(uint16_t address, uint8_t value);

        // Initialize function to load all ROMS passed in via config file, initialize RAM
        bool initialize(const std::string& D1541LoROM, const std::string& D1541HiROM);

        inline D1541VIA& getVIA1() { return via1; }
        inline const D1541VIA& getVIA1() const { return via1; }

        inline D1541VIA& getVIA2() { return via2; }
        inline const D1541VIA& getVIA2() const { return via2; }


    protected:

    private:

        // Non-owning pointers
        Logging* logger = nullptr;
        Peripheral* parentPeripheral;

        // VIA objects
        D1541VIA via1;
        D1541VIA via2;

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
        static const uint16_t VIA1_END = 0x1BFF;
        static const uint16_t VIA2_START = 0x1C00;
        static const uint16_t VIA2_END =  0x1FFF;

        // ROM Loader
        bool loadROM(const std::string& filename, std::vector<uint8_t>& targetBuffer, size_t expectedSize, const std::string& romName);
};

#endif // D1541MEMORY_H
