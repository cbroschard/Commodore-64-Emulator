// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.

#ifndef AMD29F800FLASH_H
#define AMD29F800FLASH_H

#include <array>
#include <cstddef>
#include <cstdint>
#include "StateWriter.h"
#include "StateReader.h"

class AMD29F800Flash
{
    public:
        // TODO add timing update based on NTSC/PAL
        static constexpr uint32_t FLASH_SIZE = 0x100000;
        static constexpr uint32_t SECTOR_COUNT = 19;
        static constexpr uint64_t PROGRAM_CYCLES = 8;
        static constexpr uint64_t SECTOR_ERASE_CYCLES = 1022727;
        static constexpr uint64_t CHIP_ERASE_CYCLES = 19431813;
        static constexpr uint64_t ERASE_TIMEOUT_CYCLES = 52;

        AMD29F800Flash();
        ~AMD29F800Flash();

        void savePayload(StateWriter& wrtr) const;
        bool loadPayload(StateReader& rdr);

        void reset();

        void tick();

        uint8_t read(uint32_t address);
        void write(uint32_t address, uint8_t value);

        uint8_t readFlash(uint16_t address);
        void writeFlash(uint16_t address, uint8_t value);

        void loadImage(const uint8_t* data, size_t size);

        void tick(uint64_t cycles);

        bool isDirty() const;
        void clearDirty();

        void setSectorProtected(uint32_t sector, bool protectedState);
        bool isSectorProtected(uint32_t sector) const;

    private:
        enum class FlashState
        {
            Read,
            Unlock1,
            Unlock2,
            Autoselect,
            Program,
            EraseUnlock1,
            EraseUnlock2,
            EraseConfirm,
            EraseTimeout,
            ProgramBusy,
            EraseBusy,
            EraseSuspended
        };

        enum class FlashOperation
        {
            None,
            Program,
            SectorErase,
            ChipErase
        };

        FlashState state;
        FlashOperation pendingOperation;

        std::array<uint8_t, FLASH_SIZE> flashData{};

        std::array<bool, SECTOR_COUNT> selectedSectors{};
        std::array<bool, SECTOR_COUNT> sectorProtected{};

        uint64_t eraseTimeoutRemaining;

        uint32_t operationAddress;
        uint8_t programmedValue;

        uint64_t operationCyclesRemaining;

        bool toggleDQ2;
        bool toggleDQ6;
        bool operationFailed;
        bool dirty;

        void programByte(uint32_t address, uint8_t value);

        void eraseSector(uint32_t address);
        void eraseChip();

        int findSector(uint32_t address) const;

        void startProgram(uint32_t address, uint8_t value);
        void startChipErase();

        uint8_t readBusyStatus(uint32_t address);

        bool isEraseTarget(uint32_t address) const;

        void startSectorEraseTimeout(uint32_t address);
        void selectEraseSector(uint32_t address);
        void beginSectorErase();
        void completeSectorErase();
};

#endif // AMD29F800FLASH_H
