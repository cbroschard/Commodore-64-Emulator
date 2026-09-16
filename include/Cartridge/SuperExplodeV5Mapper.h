// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef SUPEREXPLODEV5MAPPER_H
#define SUPEREXPLODEV5MAPPER_H

#include "Cartridge/CartridgeMapper.h"

class SuperExplodeV5Mapper: public CartridgeMapper
{
    public:
        SuperExplodeV5Mapper();
        virtual ~SuperExplodeV5Mapper();

        // State management
        void saveState(StateWriter& wrtr) const override;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr) override;

        uint8_t read(uint16_t address) override;
        void write(uint16_t address, uint8_t value) override;

        void reset() override;

        void tick(uint32_t elapsedCycles) override;

        bool loadIntoMemory(uint8_t bank) override;

        bool applyMappingAfterLoad() override;

        bool romReadHandledByMapper(uint16_t address) const override;
        bool romWriteEnabled(uint16_t address) const override;

    private:
        uint8_t selectedBank;
        uint64_t exromDisableCountdown;
        bool exromActive;

        // Helpers
        void updateLines();
        void restartExromTimer();
        void refreshExrom();
};

#endif // SUPEREXPLODEV5MAPPER_H
