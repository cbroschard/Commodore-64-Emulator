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
#include "Cartridge/IHasButton.h"
#include "Cartridge/IDE64/IDE64Controller.h"
#include "Cartridge/IDE64/IDE64RTC.h"

class IDE64Mapper : public CartridgeMapper, public IHasButton
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

        // Reset button
        inline uint32_t getButtonCount() const override { return 1; }
        const char* getButtonName(uint32_t buttonIndex) const override;
        void pressButton(uint32_t index) override;

        void pressReset();

    protected:

    private:
        IDE64Controller controller;
        IDE64RTC rtc;

        static const uint16_t IDE64_Controller_Start = 0xDE20;
        static const uint16_t IDE64_Controller_End   = 0xDE31;
        static const uint16_t RTC_Address            = 0xDE5F;
        static const uint16_t IDE64_Ctrl_Cfg_Start   = 0xDEFB;
        static const uint16_t IDE64_Ctrl_Cfg_End     = 0xDEFF;

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
};

#endif // IDE64MAPPER_H
