// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef RGCDMAPPER_H
#define RGCDMAPPER_H

#include "Cartridge/CartridgeMapper.h"

class RGCDMapper : public CartridgeMapper
{
    public:
        RGCDMapper();
        virtual ~RGCDMapper();

        // State management
        void saveState(StateWriter& wrtr) const override;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr) override;

        uint8_t read(uint16_t address) override;
        void write(uint16_t address, uint8_t value) override;

        bool loadIntoMemory(uint8_t bank) override;

        void reset() override;

    protected:

    private:
        uint8_t rgcdBank;
        bool disabled;

        bool applyMappingAfterLoad() override;

        bool isHuckyRevision() const;
        uint8_t resolvePhysicalBank(uint8_t requestedBank) const;
};

#endif // RGCDMAPPER_H
