// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef C64GAMESYSTEMMAPPER_H
#define C64GAMESYSTEMMAPPER_H

#include "Cartridge/CartridgeMapper.h"

class C64GameSystemMapper : public CartridgeMapper
{
    public:
        C64GameSystemMapper();
        virtual ~C64GameSystemMapper();

        // State management
        void saveState(StateWriter& wrtr) const override;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr) override;

        uint8_t read(uint16_t address) override;
        void write(uint16_t address, uint8_t value) override;
        bool applyMappingAfterLoad() override;

        bool loadIntoMemory(uint8_t bank) override;

    protected:

    private:

        uint8_t selectedBank;
};

#endif // C64GAMESYSTEMMAPPER_H
