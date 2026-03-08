// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef ATOMICPOWERMAPPER_H
#define ATOMICPOWERMAPPER_H

#include "Cartridge/CartridgeMapper.h"
#include "Cartridge/IHasButton.h"

class AtomicPowerMapper : public CartridgeMapper, public IHasButton
{
    public:
        AtomicPowerMapper();
        virtual ~AtomicPowerMapper();

        struct APControl
        {
            uint8_t raw         = 0x00;

            uint8_t bank        = 0;
            bool cartDisable    = false;
            bool ramEnable      = false;
            bool freezeClear    = false;
            bool exromHigh      = false;;
            bool gameLow        = false;

            void save(StateWriter& wrtr) const;
            bool load(StateReader& rdr);

            void decode();
        } ctrl;

        // State management
        void saveState(StateWriter& wrtr) const override;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr) override;

        inline uint32_t getButtonCount() const override { return 2; }
        const char* getButtonName(uint32_t buttonIndex) const override;
        void pressButton(uint32_t buttonIndex) override;

        uint8_t read(uint16_t address) override;
        void write(uint16_t address, uint8_t value) override;

        bool loadIntoMemory(uint8_t bank) override;

    protected:

    private:
        bool    freezeActive;
        uint8_t preFreezeRaw;
        uint8_t preFreezeBank;
        uint8_t selectedBank;
        uint8_t loadedBank;
        bool    ramEnabled;

        bool applyMappingAfterLoad() override;

        void pressFreeze();
        void pressReset();
};

#endif // ATOMICPOWERMAPPER_H
