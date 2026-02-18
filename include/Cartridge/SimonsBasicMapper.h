// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef SIMONSBASICMAPPER_H
#define SIMONSBASICMAPPER_H

#include "Cartridge/CartridgeMapper.h"

class SimonsBasicMapper : public CartridgeMapper
{
    public:
        SimonsBasicMapper();
        virtual ~SimonsBasicMapper();

        // State management
        void saveState(StateWriter& wrtr) const override;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr) override;
        bool applyMappingAfterLoad() override;

        uint8_t read(uint16_t address) override;
        void write(uint16_t address, uint8_t value) override;

        bool loadIntoMemory(uint8_t bank) override;

    protected:

    private:

        bool highROMEnabled;
};

#endif // SIMONSBASICMAPPER_H
