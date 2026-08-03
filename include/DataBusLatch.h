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
            SID,
            VIC
        };

        // State management
        void saveState(StateWriter& wrtr) const;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr);

        void drive(uint8_t value, Driver driver);

        uint8_t sample() const;

        Driver getLastDriver() const;

        void reset();

    protected:

    private:
        uint8_t latchedValue;
        Driver lastDriver;
};

#endif // DATABUSLATCH_H
