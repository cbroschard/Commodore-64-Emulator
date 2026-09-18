// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef UniversalCartridge2Mapper_H
#define UniversalCartridge2Mapper_H

#include "Cartridge/CartridgeMapper.h"

class UniversalCartridge2Mapper : public CartridgeMapper
{
    public:
        UniversalCartridge2Mapper();
        virtual ~UniversalCartridge2Mapper();

        // State management
        void saveState(StateWriter& wrtr) const override;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr) override;

        void reset() override;

        uint8_t read(uint16_t address) override;
        void write(uint16_t address, uint8_t value) override;

        uint8_t peek(uint16_t address) const override;

        bool readDrivesBus(uint16_t address) const override;

        bool loadIntoMemory(uint8_t bank) override;

        bool applyMappingAfterLoad() override;

        bool cpuReadHandledByMapper(uint16_t address) const override;
        CartridgeWriteRoute cpuWriteRoute(uint16_t address) const override;

        bool romReadHandledByMapper(uint16_t address) const override;

    private:
        struct UC2Control
        {
            uint8_t bank = 0;
            bool maxMode = false;
            bool ioDisabled = false;
            bool sramWriteEnabled = false;
            bool sramSelected = false;
            bool gameHigh = false;
            bool exromHigh = false;
        };

        enum class UC2Mode
        {
            Mode16K,
            Mode8K,
            Ultimax,
            Off
        };

        UC2Control ctrl;

        // Helpers
        void decodeBank(uint8_t value);
        uint8_t encodeControl() const;
        void decodeControl(uint8_t value);

        UC2Mode getMode() const;

        void updateLines();

        size_t ramLowOffset(uint16_t address) const;
        size_t ramHighOffset(uint16_t address) const;

        size_t ioRamOffset(uint16_t address) const;
};

#endif // UniversalCartridge2Mapper_H
