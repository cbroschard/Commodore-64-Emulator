// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef D4040MEMORY_H
#define D4040MEMORY_H

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <vector>
#include "Chip/MOS6532.h"
#include "Peripheral.h"

class D4040Memory
{
    public:
        D4040Memory();
        virtual ~D4040Memory();

        uint8_t read(uint16_t address);
        void write(uint16_t address, uint8_t value);

        uint8_t peek(uint16_t address) const;

        // Initialize function to load all ROMS passed in via config file, initialize RAM
        bool initialize(const std::string& D4040LoROM, const std::string& D4040HiROM1, const std::string& D4040HiROM2);

    private:
        // Non-owning pointers
        Peripheral* parentPeripheral;

        MOS6532 riot1;
        MOS6532 riot2;

        // 6532 RIOT RAM
        static constexpr uint16_t RIOT1_START          = 0x0000;
        static constexpr uint16_t RIOT1_END            = 0x007F;
        static constexpr uint16_t RIOT2_START          = 0x0080;
        static constexpr uint16_t RIOT2_END            = 0x00FF;

        // RIOT RAM mirrors
        static constexpr uint16_t RIOT_RAM_MIRROR_START = 0x0100;
        static constexpr uint16_t RIOT_RAM_MIRROR_END   = 0x01FF;

        // RIOT I/O registers and mirrors
        static constexpr uint16_t RIOT1_IO_START       = 0x0200;
        static constexpr uint16_t RIOT1_IO_END         = 0x027F;
        static constexpr uint16_t RIOT2_IO_START       = 0x0280;
        static constexpr uint16_t RIOT2_IO_END         = 0x02FF;

        static constexpr uint16_t RIOT_IO_MIRROR_START = 0x0300;
        static constexpr uint16_t RIOT_IO_MIRROR_END   = 0x03FF;

        // Shared RAM: four physical 1 KB banks
        static constexpr uint16_t RAM_SIZE             = 0x1000; // 4 KB

        static constexpr uint16_t SHARED_RAM1_START    = 0x1000;
        static constexpr uint16_t SHARED_RAM1_END      = 0x13FF;

        static constexpr uint16_t SHARED_RAM2_START    = 0x2000;
        static constexpr uint16_t SHARED_RAM2_END      = 0x23FF;

        static constexpr uint16_t SHARED_RAM3_START    = 0x3000;
        static constexpr uint16_t SHARED_RAM3_END      = 0x33FF;

        static constexpr uint16_t SHARED_RAM4_START    = 0x4000;
        static constexpr uint16_t SHARED_RAM4_END      = 0x43FF;

        // Shared RAM mirrors within each 4 KB window
        static constexpr uint16_t SHARED_RAM1_MIRROR_END = 0x1FFF;
        static constexpr uint16_t SHARED_RAM2_MIRROR_END = 0x2FFF;
        static constexpr uint16_t SHARED_RAM3_MIRROR_END = 0x3FFF;
        static constexpr uint16_t SHARED_RAM4_MIRROR_END = 0x4FFF;

        // DOS ROM
        static constexpr uint16_t ROM_SIZE             = 0x1000; // 4 KB
        static constexpr uint16_t ROM_START            = 0xD000;
        static constexpr uint16_t ROM_END              = 0xFFFF;

        // Shared FDC job memory (6502 addresses)
        static constexpr uint16_t FDC_JOB_START        = 0x1003;
        static constexpr uint16_t FDC_JOB_END          = 0x1011;
        static constexpr uint16_t FDC_HEADER_START     = 0x1021;
        static constexpr uint16_t FDC_HEADER_END       = 0x1098;

        static constexpr uint16_t FDC_HEADER_SIZE      = 8;
        static constexpr uint16_t FDC_JOB_COUNT        = 15;
        static constexpr uint16_t FDC_BUFFER_SIZE      = 256;

        std::vector<uint8_t> D4040RAM;
        std::vector<uint8_t> D4040ROM1;
        std::vector<uint8_t> D4040ROM2;
        std::vector<uint8_t> D4040ROM3;

         // ROM Loader
        bool loadROM(const std::string& filename, std::vector<uint8_t>& targetBuffer, size_t expectedSize, const std::string& romName);
};

#endif // D4040MEMORY_H
