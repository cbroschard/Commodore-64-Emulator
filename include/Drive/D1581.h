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
#include "Drive/D1581.h"
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

        // Reset the drive
        void reset() override;

        // Advance drive via tick method
        void tick() override;

        // Compatibility check
        bool canMount(DiskFormat fmt) const override;

        // IEC getters
        inline bool getAtnLineLow() const { return bus ? !bus->readAtnLine() : atnLineLow; }
        inline bool getClkLineLow() const { return bus ? !bus->readClkLine() : clkLineLow; }
        inline bool getDataLineLow() const { return bus ? !bus->readDataLine() :dataLineLow; }
        inline bool getSRQAsserted() const { return srqAsserted; }

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
        inline const CPU* getDriveCPU() const { return &driveCPU; }
        inline const D1581Memory* getMemory() const { return &d1581Mem; }
        inline const FDC177x* getFDC() const { return &d1581Mem.getFDC(); }
        inline const D1581CIA* getCIA() const { return &d1581Mem.getCIA(); }
        const char* getDriveTypeName() const noexcept override { return "1581"; }
        bool isDrive() const override { return true; }

    protected:
        bool motorOn;

    private:

        // CHIPS
        CPU         driveCPU;
        D1581Memory d1581Mem;
        IRQLine     irq;

        // IECBUS
        bool atnLineLow;
        bool clkLineLow;
        bool dataLineLow;
        bool srqAsserted;

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
        void applyDataLine();
        void updateAtnAckState();
};

#endif // D1581_H
