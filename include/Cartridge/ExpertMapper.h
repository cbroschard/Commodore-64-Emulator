// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef EXPERTMAPPER_H
#define EXPERTMAPPER_H

#include "Cartridge/CartridgeMapper.h"
#include "Cartridge/ICPUAttachable.h"
#include "Cartridge/IFreezable.h"
#include "Cartridge/IHasSwitch.h"

class ExpertMapper : public CartridgeMapper, public ICPUAttachable, public IFreezable, public IHasSwitch
{
    public:
        ExpertMapper();
        virtual ~ExpertMapper();

        inline void attachCPUInstance(CPU* processor) { this->processor = processor; }

        enum class SwitchPos : uint8_t
        {
            OFF,
            ON,
            PRG
        };

        // Physical cartridge switch handling
        inline uint32_t getSwitchCount() const override { return 1; }
        inline const char* getSwitchName(uint32_t switchIndex) const override {  return (switchIndex == 0) ? "Expert Switch" : "Switch"; }
        uint32_t getSwitchPositionCount(uint32_t switchIndex) const override;
        uint32_t getSwitchPosition(uint32_t switchIndex) const override;
        void setSwitchPosition(uint32_t switchIndex, uint32_t pos) override;
        const char* getSwitchPositionLabel(uint32_t switchIndex, uint32_t pos) const override;

        // State management
        void saveState(StateWriter& wrtr) const override;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr) override;

        uint8_t read(uint16_t address) override;
        void write(uint16_t address, uint8_t value) override;

        bool loadIntoMemory(uint8_t bank) override;

        void pressFreeze() override;
        void tick(uint32_t elapsedCycles) override;

    protected:

    private:
        // Non-owning pointers
        CPU* processor;

        SwitchPos sw;

        int32_t freezeCycles;
        bool freezeActive;

        bool applyMappingAfterLoad() override;
};

#endif // EXPERTMAPPER_H
