// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef FINALCARTRIDGEPLUSMAPPER_H
#define FINALCARTRIDGEPLUSMAPPER_H

#include "Cartridge/CartridgeMapper.h"
#include "Cartridge/IHasButton.h"

class FinalCartridgePlusMapper : public CartridgeMapper, public IHasButton
{
    public:
        FinalCartridgePlusMapper();
        virtual ~FinalCartridgePlusMapper();

        // State management
        void saveState(StateWriter& wrtr) const override;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr) override;

        inline uint32_t getButtonCount() const override { return 2; }
        const char* getButtonName(uint32_t buttonIndex) const override;
        void pressButton(uint32_t buttonIndex) override;

        uint8_t read(uint16_t address) override;
        void write(uint16_t address, uint8_t value) override;

        bool loadIntoMemory(uint8_t bank) override;

        bool isRegionEnabled(CartRegion region) const override;

    protected:

    private:
        uint8_t bit7Latch; // Any writes to IO-2 range, BIT 7 is a latch that can be read back

        bool cartDisabled;
        bool rom8000BfffDisabled;
        bool e000Disabled;

        bool applyMappingAfterLoad() override;

        void pressFreeze();
        void pressReset();
};

#endif // FINALCARTRIDGEPLUSMAPPER_H
