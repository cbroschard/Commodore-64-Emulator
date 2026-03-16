// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef ACTIONREPLAY4MAPPER_H
#define ACTIONREPLAY4MAPPER_H

#include "Cartridge/CartridgeMapper.h"
#include "Cartridge/IHasButton.h"

class ActionReplay4Mapper : public CartridgeMapper, public IHasButton
{
    public:
        ActionReplay4Mapper();
        ~ActionReplay4Mapper();

        // State management
        void saveState(StateWriter& wrtr) const override;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr) override;

        uint8_t read(uint16_t address) override;
        void write(uint16_t address, uint8_t value) override;

        bool loadIntoMemory(uint8_t bank) override;

        inline uint32_t getButtonCount() const override { return 2; }
        const char* getButtonName(uint32_t buttonIndex) const override;
        void pressButton(uint32_t buttonIndex) override;

    protected:

    private:
        uint8_t selectedBank;

        struct AR4Control
        {
            uint8_t raw = 0x00;

            // Decoded state from raw
            uint8_t romBank     = 0;            // bits 0 and 4 -> bank 0-3
            bool gameLow        = false;        // bit 1: 0 = GAME low, 1 = GAME high
            bool exromLow       = true;         // bit 3: 1 = EXROM low, 0 = EXROM high
            bool cartDisabled   = false;        // bit 2: freeze-end / disable

            void save(StateWriter& wrtr) const;
            bool load(StateReader& rdr);
            void decode();
        } ctrl;

        // freeze bookkeeping
        bool freezeActive;
        AR4Control preFreezeCtrl;
        uint8_t preFreezeSelectedBank;

        void pressFreeze();
        void pressReset();

        bool applyMappingAfterLoad() override;
        void applyMappingFromControl();
};

#endif // ACTIONREPLAY4MAPPER_H
