// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef BMPDATATURBO2000MAPPER_H
#define BMPDATATURBO2000MAPPER_H

#include "Cartridge/CartridgeMapper.h"

class BMPDataTurbo2000Mapper : public CartridgeMapper
{
    public:
        BMPDataTurbo2000Mapper();
        ~BMPDataTurbo2000Mapper();

        void saveState(StateWriter& wrtr) const override;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr) override;

        void reset() override;

        uint8_t read(uint16_t address) override;
        void write(uint16_t address, uint8_t value) override;

        bool loadIntoMemory(uint8_t bank) override;

        bool applyMappingAfterLoad() override;


    private:
        bool enabled;

        void updateLines();
};

#endif // BMPDATATURBO2000MAPPER_H
