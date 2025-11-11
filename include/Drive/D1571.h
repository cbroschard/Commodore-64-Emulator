// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef D1571_H
#define D1571_H

// Forward declarations
class IECBUS;

#include <algorithm>
#include <cstring>
#include "IECBUS.h"
#include "CPU.h"
#include "IRQLine.h"
#include "Drive/D1571Memory.h"
#include "Drive/Drive.h"
#include "Drive/FloppyControllerHost.h"
#include "Floppy/Disk.h"
#include "Floppy/DiskFactory.h"

class D1571 : public Drive, public FloppyControllerHost
{
    public:
        D1571(int deviceNumber, const std::string& fileName);
        virtual ~D1571();

        void reset() override;

        // Advance drive via tick method
        void tick() override;

        // Compatibility check
        bool canMount(DiskFormat fmt) const override;

        // IEC getters
        inline bool getAtnLineLow() const { return atnLineLow; }
        inline bool getClkLineLow() const { return clkLineLow; }
        inline bool getDataLineLow() const { return dataLineLow; }
        inline bool getSRQAsserted() const { return srqAsserted; }

        // IRQ handling
        void updateIRQ();

        // Drive Mechanics
        inline void startMotor() override { motorOn = true; }
        inline void stopMotor() override { motorOn = false; }
        inline bool isMotorOn() const override { return motorOn; }

        // Floppy image handling
        inline uint8_t getCurrentTrack() const override { return currentTrack; }
        inline uint8_t getCurrentSector() const override { return currentSector; }
        inline const std::string& getLoadedDiskName() const override { return loadedDiskName; }
        inline void setDiskWriteProtected(bool on) { diskWriteProtected = on; }
        inline bool isDiskLoaded() const override { return diskLoaded; }
        bool fdcIsWriteProtected() const override;
        void loadDisk(const std::string& path) override;
        void unloadDisk() override;
        bool fdcReadSector(uint8_t track, uint8_t sector, uint8_t* buffer, size_t length) override;
        bool fdcWriteSector(uint8_t track, uint8_t sector, const uint8_t* buffer, size_t length) override;
        std::vector<unsigned char> getDirectoryListing() override;
        std::vector<unsigned char> loadFileByName(const std::string& name) override;

        // IECBUS
        void atnChanged(bool atnLow) override;
        void setAtnAckEnabled(bool enabled);
        void clkChanged(bool clkState) override;
        void dataChanged(bool dataState) override;
        void setSRQAsserted(bool state) override;
        inline bool isSRQAsserted() const override { return srqAsserted; }

        // Drive Runtime Properties
        inline void setHeadSide(bool side1) { currentSide = side1 ? 1 : 0; } // 0 = bottom, 1 = top
        inline void setDensityCode(uint8_t code) { densityCode = code & 0x03; }
        inline bool isTrack0() { return currentTrack == 0; }
        void setFastSerialBusDirection(bool output);
        void setBurstClock2MHz(bool enable);
        bool getByteReadyLow() const;
        void syncTrackFromFDC();

        // ML Monitor
        inline const CPU* getDriveCPU() const { return &driveCPU; }
        inline const D1571Memory* getMemory() const { return &d1571Mem; }
        inline const FDC177x* getFDC() const { return &d1571Mem.getFDC(); }
        inline const D1571CIA* getCIA() const { return &d1571Mem.getCIA(); }
        inline const D1571VIA* getVIA1() const { return &d1571Mem.getVIA1(); }
        inline const D1571VIA* getVIA2() const { return &d1571Mem.getVIA2(); }
        const char* getDriveTypeName() const noexcept override { return "1571"; }

    protected:
        bool motorOn;

    private:

        // Drive chips
        CPU driveCPU;
        D1571Memory d1571Mem;
        IRQLine IRQ;

        // Floppy factory
        std::unique_ptr<Disk> diskImage;

        // IECBUS
        bool atnLineLow;
        bool clkLineLow;
        bool dataLineLow;
        bool srqAsserted;

        // Drive Properties
        bool currentSide;
        bool fastSerialOutput;
        bool twoMHzMode;
        uint8_t densityCode; // 0..3

        // ATN auto-ack state
        bool atnAckEnabled;      // latched from VIA bit 4 + DDRB
        bool atnAckPullsDataLow; // current auto-ack contribution to DATA line

        // Normal DATA OUT contribution from VIA
        bool dataOutPullsLow;

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

#endif // D1571_H
