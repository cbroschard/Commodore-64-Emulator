// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef IDE64CONTROLLER_H
#define IDE64CONTROLLER_H

#include <array>
#include <cstdint>
#include "Cartridge/IDE64/IDE64BlockDevice.h"
#include "StateReader.h"
#include "StateWriter.h"

class IDE64Controller
{
    public:
        IDE64Controller();
        ~IDE64Controller();

        void attachDevice(int index, IDE64BlockDevice* device);

        void reset();

        void saveState(StateWriter& wrtr) const;
        bool loadState(StateReader& rdr);

        uint8_t readRegister(uint16_t address);
        void writeRegister(uint16_t address, uint8_t value);

    protected:

    private:
        IDE64BlockDevice* devices[2];
        IDE64BlockDevice* activeDevice;

        // ---------------------------------------------------------
        // C64-visible IDE64 controller address range
        // ---------------------------------------------------------
        static constexpr uint16_t TASKFILE_BASE         = 0xDE20;
        static constexpr uint16_t TASKFILE_END          = 0xDE2F;

        static constexpr uint16_t DATA_LO_ADDR          = 0xDE30;
        static constexpr uint16_t DATA_HI_ADDR          = 0xDE31;

        // ---------------------------------------------------------
        // taskFile[] register indexes
        // ---------------------------------------------------------

        static constexpr uint8_t REG_RESERVED0          = 0x00;

        static constexpr uint8_t REG_FEATURES           = 0x01; // write
        static constexpr uint8_t REG_ERROR              = 0x01; // read

        static constexpr uint8_t REG_SECTOR_COUNT       = 0x02;
        static constexpr uint8_t REG_LBA0               = 0x03;
        static constexpr uint8_t REG_LBA1               = 0x04;
        static constexpr uint8_t REG_LBA2               = 0x05;
        static constexpr uint8_t REG_DEVICE_HEAD        = 0x06;
        static constexpr uint8_t DEVICE_HEAD_SLAVE_BIT  = 0x10;

        static constexpr uint8_t REG_COMMAND            = 0x07; // write
        static constexpr uint8_t REG_STATUS             = 0x07; // read

        static constexpr uint8_t REG_RESERVED8          = 0x08;
        static constexpr uint8_t REG_RESERVED9          = 0x09;
        static constexpr uint8_t REG_RESERVEDA          = 0x0A;
        static constexpr uint8_t REG_RESERVEDB          = 0x0B;
        static constexpr uint8_t REG_RESERVEDC          = 0x0C;
        static constexpr uint8_t REG_RESERVEDD          = 0x0D;

        static constexpr uint8_t REG_DEVICE_CTRL        = 0x0E;
        static constexpr uint8_t REG_ALT_STATUS         = 0x0E;

        static constexpr uint8_t REG_DRIVE_ADDR         = 0x0F;

        static constexpr size_t SECTOR_SIZE             = 512;

        struct IDEBusRegisters
        {
            uint8_t taskFile[0x10] = {};   // DE20-DE2F
            uint8_t dataLo         = 0;    // DE30
            uint8_t dataHi         = 0;    // DE31
        } registers;

        enum CurrentCommand : uint8_t
        {
            NONE,
            IDENTIFY_DEVICE,
            READ_SECTORS,
            WRITE_SECTORS
        };

        enum class TransferDirection : uint8_t
        {
            NONE,
            TO_HOST,
            FROM_HOST
        };

        CurrentCommand cmd = CurrentCommand::NONE;
        TransferDirection direction = TransferDirection::NONE;

        uint8_t status;
        uint8_t error;

        std::array<uint8_t, 512> sectorBuffer{};
        uint16_t bufferIndex;
        uint16_t bufferSize;

        uint32_t currentLBA;
        uint16_t  sectorsRemaining;

        // Helpers
        inline static constexpr uint8_t regIndex(uint16_t address) { return static_cast<uint8_t>(address - TASKFILE_BASE); }

        inline bool isSlaveSelected() const { return (registers.taskFile[REG_DEVICE_HEAD] & DEVICE_HEAD_SLAVE_BIT) != 0; }
        inline int getSelectedDeviceIndex() const { return isSlaveSelected() ? 1 : 0; }
        inline IDE64BlockDevice* getSelectedDevice() const { return devices[getSelectedDeviceIndex()]; }

        void executeCommand(uint8_t value);

        void prepareIdentifyData(const IDE64BlockDevice::DeviceInfo& info);
        void setIdentifyWord(uint8_t index, uint16_t value);
        void setIdentifyString(uint8_t startIndex, uint8_t wordCount, const std::string& text);

        void finishCommandSuccess();
        void failCommand(uint8_t errorCode);

        uint32_t getCurrentLBA() const;
        uint16_t getNormalizedSectorCount() const;
        void handleReadBufferComplete();
};

#endif // IDE64CONTROLLER_H
