// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef STARDOSMAPPER_H
#define STARDOSMAPPER_H

#include "Cartridge/CartridgeMapper.h"

class StarDOSMapper : public CartridgeMapper
{
    public:
        StarDOSMapper();
        virtual ~StarDOSMapper();

        // State Management
        void saveState(StateWriter& wrtr) const override;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr) override;

        uint8_t read(uint16_t address) override;
        void write(uint16_t address, uint8_t value) override;

        bool loadIntoMemory(uint8_t bank) override;

        void tick(uint32_t elapsedCycles) override;

        bool readDrivesBus(uint16_t address) const override;
        bool romReadHandledByMapper(uint16_t address) const override;

    protected:

    private:
        static constexpr uint16_t CHARGE_INCREMENT = 16;
        static constexpr uint16_t SWITCH_THRESHOLD = 512;

        uint16_t io1Charge;
        uint16_t io2Charge;

        bool romlEnabled;
        bool loaded;

        void chargeIO1();
        void chargeIO2();
        void applyLineState();

        bool applyMappingAfterLoad() override;
};

#endif // STARDOSMAPPER_H
