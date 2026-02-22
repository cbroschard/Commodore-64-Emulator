// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef FDC177X_H
#define FDC177X_H

#include <cstdint>
#include "Peripheral.h"
#include "Drive/DriveChips.h"
#include "Drive/FloppyControllerHost.h"
#include "StateReader.h"
#include "StateWriter.h"

class FDC177x : public DriveFDCBase
{
    public:
        FDC177x();
        virtual ~FDC177x();

        inline void attachFloppyeControllerHostInstance(FloppyControllerHost* host) { this->host = host; }
        inline void attachPeripheralInstance(Peripheral* parentPeripheral) { this->parentPeripheral = parentPeripheral; }

        void saveState(StateWriter& wrtr) const;
        bool loadState(StateReader& rdr);

        void reset();
        void tick(uint32_t cycles);

        uint8_t readRegister(uint16_t address);
        void writeRegister(uint16_t address, uint8_t value);

        inline bool checkIRQActive() const override { return intrq; }
        inline bool checkDRQActive() const override { return drq; }

        inline uint8_t getCurrentTrack() const { return registers.track; }
        inline void setSectorSize(uint16_t size) { currentSectorSize = size; }

        // ML Monitor
        inline fdcRegsView getRegsView() const override
        {
            return {
                registers.status,
                registers.command,
                registers.track,
                registers.sector,
                registers.data,

                drq,
                intrq,

                currentSectorSize,
                dataIndex,

                readSectorInProgress,
                writeSectorInProgress,

                cyclesUntilEvent
            };
        }

        inline uint16_t getSectorSize() const override { return currentSectorSize; }

    protected:

    private:

        // Non-owning pointers
        FloppyControllerHost* host;
        Peripheral* parentPeripheral;

        static constexpr size_t MaxSectorSize = 1024;

        enum CommandType : uint8_t
        {
            None    = 0,
            TypeI   = 1, // Restore / Seek / Step / Step In / Step Out
            TypeII  = 2, // Read Sector / Write Sector
            TypeIII = 3, // Read Address / Read Track / Write Track
            TypeIV  = 4  // Force Interrupt
        };

        enum class CommandGroup : uint8_t
        {
            Restore      = 0x00, // 0000 h V r1 r0
            Seek         = 0x10, // 0001 h V r1 r0
            Step         = 0x20, // 001u h V r1 r0  (00lu)
            StepIn       = 0x40, // 010u h V r1 r0
            StepOut      = 0x60, // 011u h V r1 r0

            ReadSector   = 0x80, // 100m h E 0 0 (0x80 / 0x90)
            WriteSector  = 0xA0, // 101m h E P a0 (0xA0 / 0xB0)

            ReadAddress  = 0xC0, // 1100 h E 0 0
            ForceInt     = 0xD0, // 1101 I3 I2 I1 I0
            ReadTrack    = 0xE0, // 1110 h E 0 0
            WriteTrack   = 0xF0  // 1111 h E P 0
        };

        enum Status : uint8_t
        {
            busy            = 0x01,
            dataRequest     = 0x02,
            lostDataOrNotT0 = 0x04,
            crcError        = 0x08,
            recordNotFound  = 0x10,
            spinUpOrDelData = 0x20,
            writeProtect    = 0x40,
            motorOn         = 0x80
        };

        struct FDCRegs
        {
            uint8_t status;
            uint8_t command;
            uint8_t track;
            uint8_t sector;
            uint8_t data;
        } registers;

        CommandType currentType;

        uint8_t sectorBuffer[MaxSectorSize]{};
        uint16_t currentSectorSize;
        uint8_t dataIndex;
        bool readSectorInProgress;
        bool writeSectorInProgress;

        bool drq;
        bool intrq;
        int32_t cyclesUntilEvent;

        CommandType decodeCommandType(uint8_t cmd) const;

        // Command execution
        void startCommand(uint8_t cmd);

        void setDRQ(bool on);
        void setBusy(bool on);
        void setINTRQ(bool on);
};

#endif // FDC177X_H
