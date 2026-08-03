// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef REXEP256MAPPER_H
#define REXEP256MAPPER_H

#include "Cartridge/CartridgeMapper.h"

class RexEP256Mapper : public CartridgeMapper
{
    public:
        RexEP256Mapper();
        virtual ~RexEP256Mapper();

        // State Management
        void saveState(StateWriter& wrtr) const override;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr) override;

        uint8_t read(uint16_t address) override;
        void write(uint16_t address, uint8_t value) override;

        bool loadIntoMemory(uint8_t bank) override;

        void reset() override;

        bool readDrivesBus(uint16_t address) const override;

    protected:

    private:
        uint8_t selectedSocket;
        uint8_t selectedSlice;
        bool disabled;

        bool applyMappingAfterLoad() override;

         // REX-specific helper.
        bool loadSocketSlice(uint8_t socket, uint8_t slice);
};

#endif // REXEP256MAPPER_H
