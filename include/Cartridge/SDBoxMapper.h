// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef SDBOXMAPPER_H
#define SDBOXMAPPER_H

#include "Cartridge/CartridgeMapper.h"

class SDBoxMapper : public CartridgeMapper
{
    public:
        SDBoxMapper();
        ~SDBoxMapper();

        void saveState(StateWriter& wrtr) const override;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr) override;

        void reset() override;

        uint8_t read(uint16_t address) override;
        void write(uint16_t address, uint8_t value) override;

        bool loadIntoMemory(uint8_t bank) override;

        bool applyMappingAfterLoad() override;

    private:
        uint8_t control;

        uint8_t selectedBank;
        uint8_t loadedBank;

        bool sdReset;
        bool romEnabled;
        bool exROM;
        bool regEnabled; // Register status

        uint8_t registerRAM[3];

        void updateLines();
        void decodeControl(uint8_t value);
};

#endif // SDBOXMAPPER_H
