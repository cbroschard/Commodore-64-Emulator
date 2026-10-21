// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef IEEE488MAPPER_H
#define IEEE488MAPPER_H

#include "Cartridge/CartridgeMapper.h"
#include "Chip/TPI6525.h"

class IEEE488Bus;

class IEEE488Mapper : public CartridgeMapper
{
    public:
        IEEE488Mapper();
        ~IEEE488Mapper();

        inline void attachIEEE488BusInstance(IEEE488Bus* ieee488Bus) { this->ieee488Bus = ieee488Bus; }

        void saveState(StateWriter& wrtr) const override;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr) override;

        void reset() override;

        uint8_t read(uint16_t address) override;
        void write(uint16_t address, uint8_t value) override;

        bool loadIntoMemory(uint8_t bank) override;

        bool applyMappingAfterLoad() override;

    private:
        TPI6525 tpi;
        IEEE488Bus* ieee488Bus;

        void updateLines();
};

#endif // IEEE488MAPPER_H
