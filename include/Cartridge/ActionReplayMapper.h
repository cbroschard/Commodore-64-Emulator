// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef ACTIONREPLAYMAPPER_H
#define ACTIONREPLAYMAPPER_H

#include "Cartridge/CartridgeMapper.h"
#include "Cartridge/ICPUAttachable.h"

class ActionReplayMapper : public CartridgeMapper, public ICPUAttachable
{
    public:
        ActionReplayMapper();
        virtual ~ActionReplayMapper();

        inline void attachCPUInstance(CPU* processor) override { this->processor = processor; }

        // State management
        void saveState(StateWriter& wrtr) const override;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr) override;

        uint8_t read(uint16_t address) override;
        void write(uint16_t address, uint8_t value) override;

        bool loadIntoMemory(uint8_t bank) override;

        // Called from UI
        void pressFreeze();

    protected:

    private:
        // Non-owning pointers
        CPU* processor;

        uint8_t selectedBank;

        bool io1Enabled;
        bool io2RoutesToRam;

        struct ARControl
        {
            uint8_t raw = 0;

            // Derived
            bool cartDisabled = false;   // bit 2
            bool ramAtROML    = false;   // bit 5
            bool freezeReset  = false;   // bit 6 (edge/level depends on your model)
            uint8_t bank      = 0;       // from bits 3..4 (+ optional bit 7)
            bool exromHigh    = false;   // bit 1 means /EXROM high
            bool gameLow      = false;   // bit 0 means /GAME low

            void save(StateWriter& wrtr) const;
            bool load(StateReader& rdr);
        } ctrl;

        // NEW: freeze bookkeeping
        bool freezeActive;
        ARControl preFreezeCtrl{};
        uint8_t preFreezeSelectedBank;

        bool applyMappingAfterLoad() override;
        void applyMappingFromControl();

        void clearFreezeMode();
};

#endif // ACTIONREPLAYMAPPER_H
