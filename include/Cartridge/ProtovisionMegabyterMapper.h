// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef PROTOVISIONMEGABYTERMAPPER_H
#define PROTOVISIONMEGABYTERMAPPER_H

#include "Cartridge/CartridgeMapper.h"
#include "Chip/AMD29F800Flash.h"

class ProtovisionMegabyterMapper : public CartridgeMapper
{
    public:
        ProtovisionMegabyterMapper();
        ~ProtovisionMegabyterMapper();

        void saveState(StateWriter& wrtr) const override;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr) override;

        void reset() override;

        void tick(uint32_t elapsedCycles) override;

        uint8_t read(uint16_t address) override;
        void write(uint16_t address, uint8_t value) override;

        bool romReadHandledByMapper(uint16_t address) const override;
        bool romWriteEnabled(uint16_t address) const override;

        bool readDrivesBus(uint16_t address) const override;

        bool loadIntoMemory(uint8_t bank) override;

        bool applyMappingAfterLoad() override;

    private:
        AMD29F800Flash flash;

        uint8_t bankRegister;
        uint8_t modeRegister;

        uint32_t translateFlashAddress(uint16_t address) const;

        void updateLines();
};

#endif // PROTOVISIONMEGABYTERMAPPER_H
