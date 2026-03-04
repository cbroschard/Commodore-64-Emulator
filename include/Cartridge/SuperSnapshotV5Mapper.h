// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef SUPERSNAPSHOTV5MAPPER_H
#define SUPERSNAPSHOTV5MAPPER_H

#include "Cartridge/CartridgeMapper.h"
#include "Cartridge/ICPUAttachable.h"
#include "Cartridge/IFreezable.h"

class SuperSnapshotV5Mapper : public CartridgeMapper, public ICPUAttachable, public IFreezable
{
    public:
        SuperSnapshotV5Mapper();
        virtual ~SuperSnapshotV5Mapper();

        inline void attachCPUInstance(CPU* processor) override { this->processor = processor; }

        struct SS5Control
        {
            uint8_t raw = 0;

            // Derived from $DE00 (SSv5 snapshot register)
            bool enabled   = true;   // bit 3 (INVERTED): 0 = enabled, 1 = disabled
            bool exromHigh = true;   // bit 1 (INVERTED): 0 -> EXROM=1 (high), 1 -> EXROM=0 (low/asserted)
            bool gameLow   = false;  // bit 0: 0 -> GAME=0 (low/asserted), 1 -> GAME=1 (high)

            // Bank select from ROM address lines:
            // bit2 = SA14 (LSB), bit4 = SA15, bit5 = SA16
            uint8_t bank = 0;

            void save(StateWriter& wrtr) const;
            bool load(StateReader& rdr);

            void decode();
        } ctrl;

        // State management
        void saveState(StateWriter& wrtr) const override;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr) override;

        uint8_t read(uint16_t address) override;
        void write(uint16_t address, uint8_t value) override;

        bool loadIntoMemory(uint8_t bank) override;

        void pressFreeze() override;

    protected:

    private:
        // Non-owning pointers
        CPU* processor;

        uint8_t selectedBank;

        bool applyMappingAfterLoad() override;
};

#endif // SUPERSNAPSHOTV5MAPPER_H
