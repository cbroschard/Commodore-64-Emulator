// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef MAGICDESKMAPPER_H
#define MAGICDESKMAPPER_H

#include "Cartridge/CartridgeMapper.h"

class MagicDeskMapper : public CartridgeMapper
{
    public:
        MagicDeskMapper();
        virtual ~MagicDeskMapper();

        // State management
        void saveState(StateWriter& wrtr) const override;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr) override;
        bool applyMappingAfterLoad() override;

        uint8_t read(uint16_t address) override;
        void write(uint16_t address, uint8_t value) override;

        bool loadIntoMemory(uint8_t bank) override;

        // Reset function
        void reset() override;

    protected:

    private:
        uint8_t magicDeskBank;
        bool disabled;
};

#endif // MAGICDESKMAPPER_H
