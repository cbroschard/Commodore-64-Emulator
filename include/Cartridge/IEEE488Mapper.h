// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef IEEE488MAPPER_H
#define IEEE488MAPPER_H

#include "Cartridge/CartridgeMapper.h"
#include "IEEE488/IEEE488Bus.h"
#include "Chip/TPI6525.h"

class IEEE488Mapper : public CartridgeMapper
{
    public:
        IEEE488Mapper();
        ~IEEE488Mapper();

        bool attachIEEE488BusInstance(IEEE488Bus* ieee488Bus);

        void saveState(StateWriter& wrtr) const override;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr) override;

        void reset() override;

        void tick(uint32_t elapsedCycles) override;

        uint8_t read(uint16_t address) override;
        void write(uint16_t address, uint8_t value) override;

        uint8_t peek(uint16_t address) const override;

        bool loadIntoMemory(uint8_t bank) override;

        bool applyMappingAfterLoad() override;

        inline bool romReadHandledByMapper(uint16_t address) const override { return address >= 0x8000 && address <= 0x9FFF; }
        bool readDrivesBus(uint16_t address) const override;
        bool isRegionEnabled(CartRegion region) const override;

    private:
        static constexpr IEEE488Bus::DeviceId CONTROLLER_ID = 0;

        TPI6525 tpi;
        IEEE488Bus* ieee488Bus;

        inline bool isTransmitting() const { return (tpi.getPortAPins() & 0x02) !=0; }

        void updateLines();
        void updateDataLines(bool transmitting);
        void sampleDataLines();

        void sampleControlLines();

        // Signal updating
        void updateATN();
        void updateDAV();
        void updateEOI();
        void updateNRFD();
        void updateNDAC();
};

#endif // IEEE488MAPPER_H
