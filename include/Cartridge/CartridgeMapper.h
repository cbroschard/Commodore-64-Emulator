// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef CARTRIDGEMAPPER_H
#define CARTRIDGEMAPPER_H

#include <cstdint>
#include "StateReader.h"
#include "StateWriter.h"

//Forward declarations
class Cartridge;
class Memory;

class CartridgeMapper
{
    public:
        CartridgeMapper();
        virtual ~CartridgeMapper();

        // State management
        virtual void saveState(StateWriter& wrtr) const = 0;
        virtual bool loadState(const StateReader::Chunk& chunk, StateReader& rdr) = 0;
        virtual bool applyMappingAfterLoad() = 0;

        // Cartridge I/O
        virtual uint8_t read(uint16_t address) = 0;
        virtual void write(uint16_t address, uint8_t value ) = 0;

        inline virtual void attachCartridgeInstance(Cartridge* cart) { this->cart = cart; }
        inline virtual void attachMemoryInstance(Memory* mem) { this->mem = mem; }

        virtual bool loadIntoMemory(uint8_t bank) = 0;

        virtual void reset() {}

    protected:
        Cartridge* cart = nullptr;
        Memory* mem = nullptr;

        // Cartridge LO/HI location constants
        static constexpr size_t CART_LO_START = 0x8000;
        static constexpr size_t CART_HI_START = 0xA000;
        static constexpr size_t CART_HI_START1 = 0xE000;

    private:

};

#endif // CARTRIDGEMAPPER_H
