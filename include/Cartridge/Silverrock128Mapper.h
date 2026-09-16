#ifndef Silverrock128Mapper_H
// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#define Silverrock128Mapper_H

#include "Cartridge/CartridgeMapper.h"

class Silverrock128Mapper : public CartridgeMapper
{
    public:
        Silverrock128Mapper();
        virtual ~Silverrock128Mapper();

        // State management
        void saveState(StateWriter& wrtr) const override;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr) override;

        uint8_t read(uint16_t address) override;
        void write(uint16_t address, uint8_t value) override;

        void reset() override;

        bool loadIntoMemory(uint8_t bank) override;

        bool applyMappingAfterLoad() override;

    private:
        uint8_t selectedBank;

        // Helpers
        void updateLines();
};

#endif // Silverrock128Mapper_H
