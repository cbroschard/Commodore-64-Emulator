// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef D2031MEMORY_H
#define D2031MEMORY_H

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <vector>
#include "Drive/D2031VIA.h"
#include "Peripheral.h"
#include "StateReader.h"
#include "StateWriter.h"

class D2031Memory
{
    public:
        D2031Memory();
        ~D2031Memory();

        void attachPeripheralInstance(Peripheral* parentPeripheral);

        // State Management
        void saveState(StateWriter& wrtr) const;
        bool loadState(StateReader& rdr);

        // Tick to advance the VIA chips
        void tick(uint32_t cycles);

        // reset function
        void reset();

        // Interfaces to memory access
        uint8_t read(uint16_t address);
        void write(uint16_t address, uint8_t value);

        uint8_t peek(uint16_t address) const;

        // Initialize function to load all ROMS passed in via config file, initialize RAM
        bool initialize(const std::string& D2031LoROM, const std::string& D2031HiROM);

    private:
        // Non-owning pointers
        Peripheral* parentPeripheral;

        D2031VIA via1;
        D2031VIA via2;

        std::vector<uint8_t> D2031RAM;
        std::vector<uint8_t> D2031ROM1;
        std::vector<uint8_t> D2031ROM2;

        // RAM constants
        static constexpr size_t RAM_SIZE        = 0x0800; // 2K RAM
        static const uint16_t D2031_RAM_START   = 0x0000;
        static const uint16_t D2031_RAM_END     = 0x07FF;

        // ROM constants
        static constexpr size_t ROM_SIZE        = 0x2000; // 8K per ROM
        static constexpr uint16_t ROM1_START    = 0xC000       ;
        static constexpr uint16_t ROM1_END      = 0xDFFF;
        static constexpr uint16_t ROM2_START    = 0xE000;
        static constexpr uint16_t ROM2_END      = 0xFFFF;

        // VIA constants
        static constexpr uint16_t VIA1_START    = 0x1800;
        static constexpr uint16_t VIA1_END      = 0x1BFF;
        static constexpr uint16_t VIA2_START    = 0x1C00;
        static constexpr uint16_t VIA2_END      = 0x1FFF;

        // ROM Loader
        bool loadROM(const std::string& filename, std::vector<uint8_t>& targetBuffer, size_t expectedSize, const std::string& romName);
};

#endif // D2031MEMORY_H
