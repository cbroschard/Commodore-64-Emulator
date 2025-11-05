// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef D1571MEMORY_H
#define D1571MEMORY_H

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>
#include "Logging.h"
#include "Drive/D1571CIA.h"
#include "Drive/D1571VIA.h"

class D1571Memory
{
    public:
        D1571Memory();
        virtual ~D1571Memory();

        inline void attachLoggInstance(Logging* logger) { this->logger = logger; }

        void setLog(bool enable) { setLogging = enable; }

        uint8_t read(uint16_t address);
        void write(uint16_t address, uint8_t value);

        void tick();
        void reset();
        bool initialize(const std::string& romName);


    protected:

    private:

        // CHIPS
        D1571VIA via1;
        D1571VIA via2;
        D1571CIA cia;

        // Non-owning pointers
        Logging* logger;

        // Log enable/disable
        bool setLogging;

        // RAM Constants
        static constexpr size_t RAM_SIZE = 0x0800; // 2K RAM
        static const uint16_t RAM_START = 0x0000;
        static const uint16_t RAM_END = 0x07FF;

        // ROM Constants
        static constexpr size_t ROM_SIZE = 0x8000; // 32K ROM
        static const uint16_t ROM_START = 0x8000;
        static const uint16_t ROM_END  = 0xFFFF;

        // VIA Constants
        static const uint16_t VIA1_START = 0x1000;
        static const uint16_t VIA1_END = 0x1BFF;
        static const uint16_t VIA2_START = 0x1C00;
        static const uint16_t VIA2_END = 0x1FFF;

        // CIA Constants
        static const uint16_t CIA_START = 0x4000;
        static const uint16_t CIA_END = 0x7FFF;

        // FDC Constants
        static const uint16_t FDC_START = 0x2000;
        static const uint16_t FDC_END   = 0x3FFF;

        std::vector<uint8_t> D1571RAM;
        std::vector<uint8_t> D1571ROM;

        bool loadROM(const std::string& filename, std::vector<uint8_t>& targetBuffer, size_t expectedSize, const std::string& romName);
};

#endif // D1571MEMORY_H
