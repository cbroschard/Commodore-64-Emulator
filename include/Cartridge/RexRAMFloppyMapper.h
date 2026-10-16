// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef REXRAMFLOPPYMAPPER_H
#define REXRAMFLOPPYMAPPER_H

#include "Cartridge/CartridgeMapper.h"
#include "Cartridge/IHasSwitch.h"

class RexRAMFloppyMapper : public CartridgeMapper, public IHasSwitch
{
    public:
        RexRAMFloppyMapper();
        ~RexRAMFloppyMapper();

        void saveState(StateWriter& wrtr) const override;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr) override;

        void reset() override;

        uint8_t read(uint16_t address) override;
        void write(uint16_t address, uint8_t value) override;

        bool loadIntoMemory(uint8_t bank) override;

        bool applyMappingAfterLoad() override;

        bool romReadHandledByMapper(uint16_t address) const override;
        CartridgeWriteRoute cpuWriteRoute(uint16_t address) const override;

        // RAM Persistence
        bool hasPersistence() const override;
        bool savePersistence(const std::string& path) const override;
        bool loadPersistence(const std::string& path) override;

        // Toggle switch
        uint32_t getSwitchCount() const override;
        const char* getSwitchName(uint32_t switchIndex) const override;

        uint32_t getSwitchPositionCount(uint32_t switchIndex) const override;
        uint32_t getSwitchPosition(uint32_t switchIndex) const override;
        const char* getSwitchPositionLabel(uint32_t switchIndex, uint32_t pos) const override;

        void setSwitchPosition(uint32_t switchIndex, uint32_t pos) override;

    private:
        uint8_t selectedBank;

        bool ramEnabled;
        bool ramWritable;

        bool enabled;
        bool switchEnabled;

        void updateLines();
};

#endif // REXRAMFLOPPYMAPPER_H
