// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef D1581MEMORY_H
#define D1581MEMORY_H

// Forward declarations
class D1581;

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>
#include "CPUBus.h"
#include "Logging.h"
#include "Peripheral.h"
#include "Drive/D1581CIA.h"
#include "Drive/FDC177x.h"

class D1581Memory : public CPUBus
{
    public:
        D1581Memory();
        virtual ~D1581Memory();

        inline void attachLoggingInstance(Logging* logger) { this->logger = logger; }
        void attachPeripheralInstance(Peripheral* parentPeripheral);

        inline void setLog(bool enable) { setLogging = enable; }

        // API access
        uint8_t read(uint16_t address);
        void write(uint16_t address, uint8_t value);

        void reset();
        void tick();

        bool initialize(const std::string& fileName);

              // Getters
        inline D1581CIA& getCIA() { return cia; }
        inline const D1581CIA& getCIA()  const { return cia; }

        inline FDC177x& getFDC() { return fdc; }
        inline const FDC177x&  getFDC()  const { return fdc; }

    protected:

    private:

        // Chips
        D1581CIA cia;
        FDC177x  fdc;

        // Non-owning pointers
        Logging* logger;
        Peripheral* parentPeripheral;

        // RAM Constants
        static constexpr size_t RAM_SIZE    = 0x2000; // 8K RAM
        static const uint16_t RAM_START     = 0x0000;
        static const uint16_t RAM_END       = 0x1FFF;

        // ROM ConstantC
        static constexpr size_t ROM_SIZE    = 0x8000; // 32K ROM
        static const uint16_t ROM_START     = 0x8000;
        static const uint16_t ROM_END       = 0xFFFF;

        // CIA6526(I/O) CHIP
        static constexpr uint16_t CIA_START = 0x4000;
        static constexpr uint16_t CIA_END   = 0x5FFF;

        // WDC FDC 1770/1772
        static constexpr uint16_t FDC_START = 0x6000;
        static constexpr uint16_t FDC_END   = 0x7FFF;


        // ML Monitor logging
        bool setLogging;

        // Last Bus
        uint8_t lastBus;

        // CHIPS
        std::vector<uint8_t> D1581RAM;
        std::vector<uint8_t> D1581ROM;

        bool loadROM(const std::string& filename);
};

#endif // D1581MEMORY_H
