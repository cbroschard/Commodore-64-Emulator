// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef GAMEKILLERMAPPER_H
#define GAMEKILLERMAPPER_H

#include "Cartridge/CartridgeMapper.h"
#include "Cartridge/IHasButton.h"

class GameKillerMapper : public CartridgeMapper, public IHasButton
{
    public:
        GameKillerMapper();
        ~GameKillerMapper();

        void saveState(StateWriter& wrtr) const override;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr) override;

        void reset() override;

        uint8_t read(uint16_t address) override;
        void write(uint16_t address, uint8_t value) override;

        bool loadIntoMemory(uint8_t bank) override;

        bool applyMappingAfterLoad() override;

        uint32_t getButtonCount() const override;
        const char* getButtonName(uint32_t buttonIndex) const override;
        void pressButton(uint32_t index) override;

        CartridgeWriteRoute cpuWriteRoute(uint16_t address) const override;

    private:
        bool enabled;
        uint8_t disableWrites;

        void updateLines();

        void pressFreeze();
};

#endif // GAMEKILLERMAPPER_H
