// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef FINALCARTRIDGEIIIMAPPER_H
#define FINALCARTRIDGEIIIMAPPER_H

#include "Cartridge/CartridgeMapper.h"
#include "Cartridge/ICPUAttachable.h"
#include "Cartridge/IFreezable.h"

class FinalCartridgeIIIMapper : public CartridgeMapper, public ICPUAttachable, public IFreezable
{
    public:
        FinalCartridgeIIIMapper();
        virtual ~FinalCartridgeIIIMapper();

        enum class CartMode : uint8_t
        {
            cart8K,
            cart16K,
            Ultimax
        };

        struct FCIIIControl
        {
            uint8_t raw = 0;

            // Derived from raw
            uint8_t bank = 0;
            bool cartEnabled = true;
            bool ramEnabled  = false;
            bool releaseFreezeStrobe = false;
            CartMode mode = CartMode::cart8K;

            void decode();

            void save(StateWriter& wrtr) const;
            bool load(StateReader& rdr);
        };

        inline void attachCPUInstance(CPU* processor) override { this->processor = processor; }

        // State management
        void saveState(StateWriter& wrtr) const override;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr) override;

        uint8_t read(uint16_t address) override;
        void write(uint16_t address, uint8_t value) override;

        bool loadIntoMemory(uint8_t bank) override;

        void pressFreeze() override;

    protected:

    private:
        // Non-owning pointers
        CPU* processor;

        // Live mapper state
        FCIIIControl ctrl{};
        FCIIIControl preFreezeCtrl{};

        uint8_t freezeBank;
        bool freezeActive;

        bool applyMappingAfterLoad() override;

};

#endif // FINALCARTRIDGEIIIMAPPER_H
