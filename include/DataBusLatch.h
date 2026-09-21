// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef DATABUSLATCH_H
#define DATABUSLATCH_H

#include <cstdint>
#include "StateReader.h"
#include "StateWriter.h"

class DataBusLatch
{
    public:
        DataBusLatch();
        virtual ~DataBusLatch();

        enum class Driver : uint8_t
        {
            None,
            Cartridge,
            CIA1,
            CIA2,
            CPU,
            Memory,
            REU,
            SID,
            VIC
        };

        // State management
        void saveState(StateWriter& wrtr) const;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr);

        // Device drives all 8-bits
        void drive(uint8_t value, Driver driver);
        void drive(uint8_t value, Driver drive, uint64_t cycle);

        // Device only drives some bits
        void drive(uint8_t value, uint8_t driveMask, Driver driver, uint64_t cycle);

        uint8_t sample() const;
        uint8_t sample(uint64_t cycle);

        Driver getLastDriver() const;

        void reset();

    private:
        static constexpr uint64_t DEFAULT_DECAY_CYCLES = 0;

        uint8_t latchedValue;

        Driver lastDriver;

        uint64_t lastUpdateCycle;
        uint64_t decayRemaining[8];

        void updateDecay(uint64_t cycle);
};

#endif // DATABUSLATCH_H
