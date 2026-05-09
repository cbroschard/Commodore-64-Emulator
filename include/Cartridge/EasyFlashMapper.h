// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef EASYFLASHMAPPER_H
#define EASYFLASHMAPPER_H

#include <array>
#include "Cartridge/CartridgeMapper.h"

class EasyFlashMapper : public CartridgeMapper
{
    public:
        EasyFlashMapper();
        virtual ~EasyFlashMapper();

        // State management
        void saveState(StateWriter& wrtr) const override;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr) override;
        bool applyMappingAfterLoad() override;

        uint8_t read(uint16_t address) override;
        void write(uint16_t address, uint8_t value) override;

        bool loadIntoMemory(uint8_t bank) override;

    protected:

    private:

        struct EasyFlashControl
        {
            uint8_t raw = 0x05; // M=1, X=0, G=1 -> common EasyFlash boot/Ultimax-ish default

            bool led() const
            {
                return (raw & 0x80) != 0;
            }

            bool modeControl() const
            {
                return (raw & 0x04) != 0;
            }

            bool exromBit() const
            {
                return (raw & 0x02) != 0;
            }

            bool gameBit() const
            {
                return (raw & 0x01) != 0;
            }

            void set(uint8_t value)
            {
                raw = value;
            }
        };

        EasyFlashControl control;

        std::array<uint8_t, 256> dfRam{};
        uint8_t selectedBank;

        void applyControlRegister(uint8_t value);
};

#endif // EASYFLASHMAPPER_H
