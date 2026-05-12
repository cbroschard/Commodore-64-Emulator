// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef REU_H
#define REU_H

#include <cstddef>
#include <cstdint>
#include <vector>
#include "Common/REUModel.h"
#include "StateReader.h"
#include "StateWriter.h"

class REU
{
    public:
        REU();
        virtual ~REU();

        void saveState(StateWriter& wrtr) const;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr);

        void reset();

        uint8_t readIO(uint16_t addr);
        void writeIO(uint16_t addr, uint8_t value);

        inline bool isEnabled() const    { return model != REUModel::None && !ram.empty(); }
        inline REUModel getModel() const { return model; }
        void setModel(REUModel reuModel);

    protected:

    private:

        // Status Register $DF00
        static constexpr uint8_t SR_VERSION_MASK    = 0x0F; // bits 0-3
        static constexpr uint8_t SR_SIZE_FLAG       = 0x10; // bit 4
        static constexpr uint8_t SR_VERIFY_ERROR    = 0x20; // bit 5
        static constexpr uint8_t SR_END_OF_BLOCK    = 0x40; // bit 6
        static constexpr uint8_t SR_IRQ_PENDING     = 0x80; // bit 7

        // Command Register $DF01
        static constexpr uint8_t CR_TRANSFER_MASK   = 0x03; // bits 0-1
        static constexpr uint8_t CR_RESERVED_MODE   = 0x1C; // bits 2-4
        static constexpr uint8_t CR_AUTOLOAD        = 0x20; // bit 5
        static constexpr uint8_t CR_UNUSED_40       = 0x40; // bit 6
        static constexpr uint8_t CR_EXECUTE         = 0x80; // bit 7

        // REU Bank Register $DF06
        static constexpr uint8_t RB_BANK_MASK_512K    = 0x07; // A18-A16 on stock 512 KB REU

        // IRQ Mask Register $DF09
        static constexpr uint8_t IRQ_ENABLE          = 0x80; // bit 7
        static constexpr uint8_t IRQ_END_OF_BLOCK    = 0x40; // bit 6
        static constexpr uint8_t IRQ_VERIFY_ERROR    = 0x20; // bit 5
        static constexpr uint8_t IRQ_UNUSED_MASK     = 0x1F; // bits 0-4

        // Address Control Register $DF0A
        static constexpr uint8_t ACR_FIX_C64         = 0x80; // bit 7
        static constexpr uint8_t ACR_FIX_REU         = 0x40; // bit 6
        static constexpr uint8_t ACR_UNUSED_MASK     = 0x3F; // bits 0-5

        std::vector<uint8_t> ram;

        struct REURegisters
        {
            uint8_t status         = 0x00;
            uint8_t command        = 0x00;

            uint16_t c64Address    = 0x0000;

            uint16_t reuAddressLo  = 0x0000;
            uint8_t  reuBank       = 0x00;

            uint16_t transferLen   = 0x0000;

            uint8_t irqMask        = 0x00;
            uint8_t addressControl = 0x00;
        };

        REURegisters regs;

        REUModel model;

        // Helpers
        uint8_t baseStatusForModel() const;

        uint32_t reuAddress() const;
        uint32_t maskedREUAddress() const;

        uint32_t transferLengthBytes() const;

        bool shouldIncrementC64Address() const;
        bool shouldIncrementREUAddress() const;
        void incrementREUAddress();

        void updateIRQStatus();

        void startTransfer();
};

#endif // REU_H
