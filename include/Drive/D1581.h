// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef D1581_H
#define D1581_H

// Forward declarations
class IECBUS;

#include "CPU.h"
#include "Drive/Drive.h"
#include "Drive/D1581Memory.h"
#include "Drive/FloppyControllerHost.h"
#include "Floppy/Disk.h"
#include "Floppy/DiskFactory.h"
#include "IRQLine.h"

class D1581 : public Drive, public FloppyControllerHost
{
    public:
        D1581(int deviceNumber, const std::string& romNAME);
        virtual ~D1581();

        inline uint32_t clockMultiplier() const override { return 2; }

        // Reset the drive
        void reset() override;

        // Advance drive via tick method
        void tick(uint32_t cycles) override;

        // Drive Model
        inline DriveModel getDriveModel() const override { return DriveModel::D1581; }

        // Get disk path
        std::string getCurrentDiskPath() const override { return isDiskLoaded() ? loadedDiskName : std::string{}; }

        // Compatibility check
        inline bool canMount(DiskFormat fmt) const override { return fmt == DiskFormat::D81; }
        inline uint8_t getCurrentSide() const { return currentSide; }
        inline void setCurrentSide(uint8_t side) { currentSide = side; }

        // status
        inline DriveStatus getDriveStatus() const override { return currentDriveStatus; }

        // Chip getters
        inline CPU* getDriveCPU() override { return &driveCPU; }
        inline DriveMemoryBase* getMemory() override { return &d1581mem; }
        inline const DriveVIABase* getVIA1() const override { return nullptr; }
        inline const DriveVIABase* getVIA2() const override { return nullptr; }

        // FDC Sync
        void syncTrackFromFDC();

        void unloadDisk() override;
        void forceSyncIEC() override;

        void atnChanged(bool atnLow) override;
        void clkChanged(bool clkLow)  override;
        void dataChanged(bool dataLow) override;

        inline bool isSRQAsserted() const override { return srqAsserted; }
        inline void setSRQAsserted(bool srq) override { srqAsserted = srq; }

        // IEC Commands
        void onListen() override;
        void onUnListen() override;
        void onTalk() override;
        void onUnTalk() override;
        void onSecondaryAddress(uint8_t sa) override;

        // IRQ handling
        void updateIRQ();

        // IEC getters
        inline bool getAtnLineLow() const override { return bus ? !bus->readAtnLine() : atnLineLow; }
        inline bool getClkLineLow() const override { return bus ? !bus->readClkLine() : clkLineLow; }
        inline bool getDataLineLow() const override  { return bus ? !bus->readDataLine() :dataLineLow; }
        inline bool getSRQAsserted() const override { return srqAsserted; }

        // Drive Mechanics
        inline void startMotor() override { motorOn = true; }
        inline void stopMotor() override { motorOn = false; }
        inline bool isMotorOn() const override { return motorOn; }

        // Floppy Image
        inline uint8_t getCurrentTrack() const override { return currentTrack; }
        inline uint8_t getCurrentSector() const override { return currentSector; }
        inline void setDiskWriteProtected(bool on) { diskWriteProtected = on; }
        inline bool isDiskLoaded() const override { return diskLoaded; }
        inline const std::string& getLoadedDiskName() const override { return loadedDiskName; }

        // ML Monitor
        inline bool hasCIA() const override { return true; }
        inline bool hasVIA1() const override { return false; }
        inline bool hasVIA2() const override { return false; }
        inline bool hasFDC() const override  { return true; }
        inline const CPU* getDriveCPU() const override { return &driveCPU; }
        inline const D1581Memory* getMemory() const override { return &d1581mem; }
        inline const FDC177x* getFDC() const override { return &d1581mem.getFDC(); }
        inline const DriveCIA* getCIA() const override { return &d1581mem.getCIA(); }
        const char* getDriveTypeName() const noexcept override { return "1581"; }
        bool isDrive() const override { return true; }

    protected:
        bool motorOn;

    private:

        // CHIPS
        CPU         driveCPU;
        D1581Memory d1581mem;
        IRQLine     irq;

        std::unique_ptr<Disk> diskImage;

        uint8_t currentSide;

        inline bool fdcIsWriteProtected() const override { return diskWriteProtected; }
        bool fdcReadSector(uint8_t track, uint8_t sector, uint8_t* buffer, size_t length) override;
        bool fdcWriteSector(uint8_t track, uint8_t sector, const uint8_t* buffer, size_t length) override;

        void loadDisk(const std::string& path) override;

        // IECBUS
        bool atnLineLow;
        bool clkLineLow;
        bool dataLineLow;
        bool srqAsserted;
        bool iecLinesPrimed;
        bool iecListening;
        bool iecRxActive;
        bool iecTalking;
        bool expectingSecAddr;
        bool expectingDataByte;
        uint8_t currentListenSA;
        uint8_t currentTalkSA;

        int iecRxBitCount;
        uint8_t iecRxByte;

        // Floppy Image
        std::string loadedDiskName;
        bool diskLoaded;
        bool diskWriteProtected;

        // Status tracking
        DriveError lastError;
        DriveStatus status;

        // Drive geometry
        uint8_t currentTrack;
        uint8_t currentSector;

        // Helpers
        uint16_t mapFdcTrackToD81Track(uint8_t fdcTrack) const;
};

#endif // D1581_H
