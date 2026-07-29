// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef DELAEP256MAPPER_H
#define DELAEP256MAPPER_H

#include "Cartridge/CartridgeMapper.h"

class DelaEP256Mapper : public CartridgeMapper
{
    public:
        DelaEP256Mapper();
        virtual ~DelaEP256Mapper();

        // State Management
        void saveState(StateWriter& wrtr) const override;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr) override;

        uint8_t read(uint16_t address) override;
        void write(uint16_t address, uint8_t value) override;

        bool loadIntoMemory(uint8_t bank) override;

        void reset() override;

    protected:

    private:
        uint8_t selectedBank;
        bool disabled;

        bool applyMappingAfterLoad() override;

        bool decodeBank(uint8_t value, uint8_t& bank) const;
};

#endif // DELAEP256MAPPER_H
