// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef FINALCARTRIDGEMAPPER_H
#define FINALCARTRIDGEMAPPER_H

#include "Cartridge/CartridgeMapper.h"
#include "Cartridge/ICPUAttachable.h"
#include "Cartridge/IFreezable.h"

class FinalCartridgeMapper : public CartridgeMapper, public ICPUAttachable, public IFreezable
{
    public:
        FinalCartridgeMapper();
        virtual ~FinalCartridgeMapper();

        inline void attachCPUInstance(CPU* processor) override { this->processor = processor; }

        // State management
        void saveState(StateWriter& wrtr) const override;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr) override;

        uint8_t read(uint16_t address) override;
        void write(uint16_t address, uint8_t value) override;

        bool loadIntoMemory(uint8_t bank) override;

        void pressFreeze() override;

    protected:

    private:
        // Non-owning pointers
        CPU* processor;

        bool cartEnabled;

        bool applyMappingAfterLoad() override;
};

#endif // FINALCARTRIDGEMAPPER_H
