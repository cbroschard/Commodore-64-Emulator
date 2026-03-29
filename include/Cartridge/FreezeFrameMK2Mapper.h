// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef FREEZEFRAMEMK2MAPPER_H
#define FREEZEFRAMEMK2MAPPER_H

#include "Cartridge/CartridgeMapper.h"
#include "Cartridge/IHasButton.h"

class FreezeFrameMK2Mapper : public CartridgeMapper, public IHasButton
{
    public:
        FreezeFrameMK2Mapper();
        ~FreezeFrameMK2Mapper();

        void saveState(StateWriter& wrtr) const override;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr) override;

        uint8_t read(uint16_t address) override;
        void write(uint16_t address, uint8_t value) override;

        virtual void reset() override;

        bool loadIntoMemory(uint8_t bank) override;

        // Reset button
        inline uint32_t getButtonCount() const override { return 1; }
        const char* getButtonName(uint32_t buttonIndex) const override;
        void pressButton(uint32_t index) override;

        void pressFreeze();

    protected:

    private:
        uint8_t selectedBank;

        bool applyMappingAfterLoad() override;

        // Help
        bool setBank(uint8_t bank);
};

#endif // FREEZEFRAMEMK2MAPPER_H
