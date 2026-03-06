// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef RETROREPLAYMAPPER_H
#define RETROREPLAYMAPPER_H

#include "Cartridge/CartridgeMapper.h"
#include "Cartridge/ICPUAttachable.h"
#include "Cartridge/IFreezable.h"

class RetroReplayMapper : public CartridgeMapper, public ICPUAttachable, public IFreezable
{
    public:
        RetroReplayMapper();
        virtual ~RetroReplayMapper();

        struct RRControl
        {
            uint8_t de00 = 0;
            uint8_t de01 = 0;

            // decoded from de00/de01
            bool gameAsserted = false;
            bool exromAsserted = false;
            bool cartDisabled = false;
            bool ramSelected = false;
            bool leaveFreeze = false;

            bool accessoryEnable = false;
            bool allowBank = false;
            bool noFreeze = false;
            bool reuCompat = false;

            uint8_t romBank = 0;
            uint8_t ramBank = 0;

            void save(StateWriter& wrtr) const;
            bool load(StateReader& rdr);
            void decode(bool flashMode);
        } ctrl;

        inline void attachCPUInstance(CPU* processor) override { this->processor = processor; }

        // State management
        void saveState(StateWriter& wrtr) const override;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr) override;

        uint8_t read(uint16_t address) override;
        void write(uint16_t address, uint8_t value) override;

        bool loadIntoMemory(uint8_t bank) override;

        // Called from UI
        void pressFreeze() override;

    protected:

    private:
        // Non-owning pointers
        CPU* processor;

        bool freezePending;
        bool freezeActive;
        bool registersLocked;
        bool de01Locked;
        bool flashMode;

        bool applyMappingAfterLoad() override;
};

#endif // RETROREPLAYMAPPER_H
