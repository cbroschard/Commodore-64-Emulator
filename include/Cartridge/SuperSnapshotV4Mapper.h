// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef SUPERSNAPSHOTV4MAPPER_H
#define SUPERSNAPSHOTV4MAPPER_H

#include "Cartridge/CartridgeMapper.h"
#include "Cartridge/IHasButton.h"

class SuperSnapshotV4Mapper : public CartridgeMapper, public IHasButton
{
    public:
        SuperSnapshotV4Mapper();
        virtual ~SuperSnapshotV4Mapper();

        struct SS4Control
        {
            // --- Raw regs ---
            uint8_t df00 = 0x00;        // ROM config (write-only in hw, but store it)
            uint8_t df01 = 0x00;        // RAM config (read/write)
            uint8_t df00Last = 0x00;
            uint8_t df01Last = 0x00;

            // --- Derived ---
            uint8_t bank = 0;           // df00 bit2 (0..1)
            bool cartDisabled = false;  // df00 bit3

            // Mapping mode (mutually exclusive-ish)
            bool ultimax = false;       // special case / DF01 rule
            bool map16k  = true;        // default when bit0==0 and not ultimax special
            bool map8k   = false;       // when bit0==1 and not ultimax special

            // RAM overlay behavior
            bool ramAtROML = false;     // enabled in ultimax cases

            // Freeze lifecycle hint
            bool releaseFreeze = false; // df00 bit1 written as 1 (strobe)

            void save(StateWriter& wrtr) const;
            bool load(StateReader& rdr);

            // Call this after df00 changes (writes to $DF00)
            void decodeFromDF00();
            void applyDF01Write(uint8_t newVal);

            // Used when loading state
            void rebuildFromSavedState();
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
        SS4Control preFreezeCtrl{};

        bool freezeActive;
        uint8_t selectedBank;

        bool applyMappingAfterLoad() override;
        void pressFreeze();
        void pressReset();
};

#endif // SUPERSNAPSHOTV4MAPPER_H
