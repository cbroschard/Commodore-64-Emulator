// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef BLACKBOXV9MAPPER_H
#define BLACKBOXV9MAPPER_H

#include "Cartridge/CartridgeMapper.h"

class BlackBoxV9Mapper : public CartridgeMapper
{
    public:
        BlackBoxV9Mapper();
        virtual ~BlackBoxV9Mapper();

        // State management
        void saveState(StateWriter& wrtr) const override;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr) override;

        uint8_t read(uint16_t address) override;
        void write(uint16_t address, uint8_t value) override;

        uint8_t peek(uint16_t address) const override;

        void reset() override;

        bool loadIntoMemory(uint8_t bank) override;

        bool readDrivesBus(uint16_t address) const;

    private:
        uint8_t selectedBank;
        uint8_t loadedBank;
        bool gameHigh;
        bool exromHigh;

        void decodeControl(uint16_t address, bool isWrite);

        bool applyMappingAfterLoad() override;

        void updateLines();
};

#endif // BLACKBOXV9MAPPER_H
