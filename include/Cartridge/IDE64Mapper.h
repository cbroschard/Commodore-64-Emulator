// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef IDE64MAPPER_H
#define IDE64MAPPER_H

#include "Cartridge/CartridgeMapper.h"
#include "Cartridge/IDE64/IDE64Controller.h"
#include "Cartridge/IDE64/IDE64RTC.h"

class IDE64Mapper : public CartridgeMapper
{
    public:
        IDE64Mapper();
        ~IDE64Mapper();

        // State management
        void saveState(StateWriter& wrtr) const override;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr) override;

        void reset() override;

        uint8_t read(uint16_t address) override;
        void write(uint16_t address, uint8_t value) override;

        bool loadIntoMemory(uint8_t bank) override;

        bool hasPersistence() const override { return true; }
        bool savePersistence(const std::string& path) const override;
        bool loadPersistence(const std::string& path) override;

    protected:

    private:
        IDE64Controller controller;
        IDE64RTC rtc;

        struct ControlState
        {
            uint8_t de32Raw  = 0x10;
            uint8_t romBankRegs[4] = {};     // DE32-DE35 view
            uint8_t memCfg[4] = {};          // DEFC-DEFF

            bool exrom = false;
            bool game = false;
            bool romAddr14 = false;
            bool romAddr15 = false;
            bool versionBit = true;
            bool killed = false;

            void decodeDE32();
            void composeDE32();

            void save(StateWriter& wrtr) const;
            bool load(StateReader& rdr);
        } ctrl;

        // Storage
        std::vector<uint8_t> rom;
        std::vector<uint8_t> ram;
        std::vector<uint8_t> flashCfg;

        uint8_t readControlRegister(uint16_t address) const;
        void writeControlRegister(uint16_t address, uint8_t value);

        bool applyMappingAfterLoad() override;
        void applyMappingFromControl();
};

#endif // IDE64MAPPER_H
