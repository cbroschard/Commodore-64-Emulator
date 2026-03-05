// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef WARPSPEEDMAPPER_H
#define WARPSPEEDMAPPER_H

#include "Cartridge/CartridgeMapper.h"

class WarpSpeedMapper : public CartridgeMapper
{
    public:
        WarpSpeedMapper();
        virtual ~WarpSpeedMapper();

        // State management
        void saveState(StateWriter& wrtr) const override;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr) override;

        uint8_t read(uint16_t address) override;
        void write(uint16_t address, uint8_t value) override;

        bool loadIntoMemory(uint8_t bank) override;

    protected:

    private:
        bool enabled;

        std::array<uint8_t, 0x200> ioMirror;

        bool applyMappingAfterLoad() override;
};

#endif // WARPSPEEDMAPPER_H
