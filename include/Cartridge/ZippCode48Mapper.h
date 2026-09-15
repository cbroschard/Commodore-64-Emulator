// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef ZIPPCODE48MAPPER_H
#define ZIPPCODE48MAPPER_H

#include "Cartridge/CartridgeMapper.h"

class ZippCode48Mapper : public CartridgeMapper
{
    public:
        ZippCode48Mapper();
        virtual ~ZippCode48Mapper();

        // State management
        void saveState(StateWriter& wrtr) const override;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr) override;

        uint8_t read(uint16_t address) override;
        void write(uint16_t address, uint8_t value) override;

        uint8_t peek(uint16_t address) const override;

        bool readDrivesBus(uint16_t address) const override;

        void reset() override;

        bool loadIntoMemory(uint8_t bank) override;

        bool applyMappingAfterLoad() override;

    private:
        bool enabled;

        void updateLines();
};

#endif // ZIPPCODE48MAPPER_H
