// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef PAGEFOXMAPPER_H
#define PAGEFOXMAPPER_H

#include "Cartridge/CartridgeMapper.h"

class PagefoxMapper : public CartridgeMapper
{
    public:
        PagefoxMapper();
        virtual ~PagefoxMapper();

        // State management
        void saveState(StateWriter& wrtr) const override;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr) override;

        void reset() override;

        uint8_t read(uint16_t address) override;
        void write(uint16_t address, uint8_t value) override;

        uint8_t peek(uint16_t address) const override;

        bool loadIntoMemory(uint8_t bank) override;

        bool applyMappingAfterLoad() override;

        bool romReadHandledByMapper(uint16_t address) const override;

        CartridgeWriteRoute cpuWriteRoute(uint16_t address) const override;

        bool readDrivesBus(uint16_t address) const override;

    private:
        enum class PagefoxChip : uint8_t
        {
            Eprom79 = 0,
            EpromZS3 = 1,
            RAM = 2,
            Empty = 3
        };

        struct PagefoxControl
        {
            uint8_t bankSelect = 0;
            PagefoxChip chip = PagefoxChip::Eprom79;
            bool enabled = true;
        };

        PagefoxControl ctrl;

        // Helpers
        void decodeControl(uint8_t value);

        uint8_t romBank() const;

        void updateLines();

        size_t ramLowOffset(uint16_t address) const;
        size_t ramHighOffset(uint16_t address) const;
};

#endif // PAGEFOXMAPPER_H
