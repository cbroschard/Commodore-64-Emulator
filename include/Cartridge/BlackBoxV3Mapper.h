// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef BLACKBOXV3MAPPER_H
#define BLACKBOXV3MAPPER_H

#include "Cartridge/CartridgeMapper.h"

class BlackBoxV3Mapper : public CartridgeMapper
{
    public:
        BlackBoxV3Mapper();
        virtual ~BlackBoxV3Mapper();

        // State management
        void saveState(StateWriter& wrtr) const override;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr) override;

        uint8_t read(uint16_t address) override;
        void write(uint16_t address, uint8_t value) override;

       void reset() override;

        bool loadIntoMemory(uint8_t bank) override;

    private:
        bool enabled;

        bool applyMappingAfterLoad() override;

        void updateLines();
};

#endif // BLACKBOXV3MAPPER_H
