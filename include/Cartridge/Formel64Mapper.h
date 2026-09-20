// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef FORMEL64MAPPER_H
#define FORMEL64MAPPER_H

#include "Cartridge/CartridgeMapper.h"
#include "Cartridge/IHasButton.h"
#include "Chip/MC6821.h"

class Formel64Mapper : public CartridgeMapper, public IHasButton
{
    public:
        Formel64Mapper();
        virtual ~Formel64Mapper();

        // State management
        void saveState(StateWriter& wrtr) const override;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr) override;

        void reset() override;

        inline void tick(uint32_t elapsedCycles) override { pia.tick(elapsedCycles); }

        uint8_t read(uint16_t address) override;
        void write(uint16_t address, uint8_t value) override;

        uint8_t peek(uint16_t address) const override;

        bool loadIntoMemory(uint8_t bank) override;

        bool applyMappingAfterLoad() override;

        inline uint32_t getButtonCount() const override { return 1; }
        const char* getButtonName(uint32_t buttonIndex) const override;
        void pressButton(uint32_t buttonIndex) override;

        bool cpuReadHandledByMapper(uint16_t address) const override;
        CartridgeWriteRoute cpuWriteRoute(uint16_t address) const override;
        bool readDrivesBus(uint16_t address) const override;

    private:
        MC6821 pia;

        bool piaControlsMapping;

        inline uint8_t romBank() const { return (pia.getPortBOutputLatch() >> 1) & 0x03; }
        inline bool romEnabled() const { return (pia.getPortBOutputLatch() & 0x08) != 0; }
        inline bool effectiveRomEnabled() const { return !piaControlsMapping || romEnabled(); }
        inline uint8_t effectiveRomBank() const { return piaControlsMapping ? romBank() : 0; }

        void updateLines();
};

#endif // FORMEL64MAPPER_H
