// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef GMOD2MAPPER_H
#define GMOD2MAPPER_H

#include <vector>
#include "Cartridge/CartridgeMapper.h"
#include "EEPROM/SerialEEPROM93C86.h"

class GMod2Mapper : public CartridgeMapper
{
    public:
        GMod2Mapper();
        ~GMod2Mapper();

        // State management
        void saveState(StateWriter& wrtr) const override;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr) override;

        uint8_t read(uint16_t address) override;
        void write(uint16_t address, uint8_t value) override;

        bool loadIntoMemory(uint8_t bank) override;

        // EEPROM support
        inline bool hasPersistence() const override { return true; }
        inline bool savePersistence(const std::string& path) const override { return eeprom.savePersistence(path); }
        inline bool loadPersistence(const std::string& path) override { return eeprom.loadPersistence(path); }
        bool romWriteEnabled(uint16_t address) const override;

    protected:

    private:
        SerialEEPROM93C86 eeprom;

        static constexpr size_t BANK_SIZE  = 0x2000;
        static constexpr size_t BANK_COUNT = 64;
        static constexpr size_t FLASH_SIZE = BANK_SIZE * BANK_COUNT;
        static constexpr size_t SECTOR_SIZE = 0x10000; // 64KB sectors for 512KB total / 8 sectors

        enum class FlashReadMode
        {
            ReadArray,
            AutoSelect
        };

        enum class FlashCmdState
        {
            Idle,
            Unlock1Seen,
            Unlock2Seen,
            ProgramSetup,
            EraseSetup,
            EraseUnlock1Seen,
            EraseUnlock2Seen
        };

        FlashReadMode flashReadMode = FlashReadMode::ReadArray;
        FlashCmdState flashCmdState = FlashCmdState::Idle;

        std::vector<uint8_t> flashData;
        bool flashDirty;
        bool flashInitialized;

        uint8_t selectedBank;

        struct G2Control
        {
            uint8_t raw = 0x00;

            // Flash bank number from bits 0-5.
            // Note: bits 4 and 5 are also shared with EEPROM DI/CLK.
            uint8_t romBank = 0;

            // Shared EEPROM control bits
            bool di = false;           // bit 4
            bool clk = false;          // bit 5
            bool cs = false;           // bit 6 (1 = EEPROM selected)

            // Expansion port control derived from bit 6
            bool exromHigh = false;    // bit 6, so EXROM active when false

            // Flash write control
            bool writeEnable = false;  // bit 7

            void decode();
            void save(StateWriter& wrtr) const;
            bool load(StateReader& rdr);
        } ctrl;

        bool applyMappingAfterLoad() override;
        void applyMappingFromControl();

        bool rebuildFlashImageFromCRT();
        void updateMappedByteIfVisible(uint8_t bank, uint16_t offset, uint8_t value);

        void resetFlashCommandState();
        uint8_t readFlashByte(uint16_t address) const;
        void handleFlashWrite(uint16_t address, uint8_t value);
        void programFlashByte(uint16_t address, uint8_t value);
        void eraseFlashSector(uint16_t address);
        void eraseFlashChip();
};

#endif // GMOD2MAPPER_H
