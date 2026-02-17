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
#include "common.h"
#include "CPU.h"
#include "Drive/D1571Memory.h"
#include "Drive/Drive.h"
#include "Drive/FloppyControllerHost.h"
#include "Floppy/Disk.h"
#include "Floppy/DiskFactory.h"
#include "IECBUS.h"

class D1571 : public Drive, public FloppyControllerHost
{
    public:
        D1571(int deviceNumber, const std::string& romName);
        virtual ~D1571();

        void reset() override;

        // Advance drive via tick method
        void tick(uint32_t cycles) override;

        // Drive Model
        inline DriveModel getDriveModel() const override { return DriveModel::D1571; }

        // Get disk path
        std::string getCurrentDiskPath() const override { return isDiskLoaded() ? loadedDiskName : std::string{}; }

        // Compatibility check
        inline bool canMount(DiskFormat fmt) const override { return fmt == DiskFormat::D64 || fmt == DiskFormat::D71; }

        // IEC getters
        inline bool getAtnLineLow()  const  override { return bus ? !bus->readAtnLine() : atnLineLow; }
        inline bool getClkLineLow()  const  override { return bus ? !bus->readClkLine() : clkLineLow; }
        inline bool getDataLineLow() const override  { return bus ? !bus->readDataLine() :dataLineLow; }
        inline bool getSRQAsserted() const override  { return srqAsserted; }

        // IRQ handling
        void updateIRQ();

        // Drive Mechanics
        inline void startMotor() override { motorOn = true; }
        inline void stopMotor() override { motorOn = false; }
        inline bool isMotorOn() const override { return motorOn; }
        void onStepperPhaseChange(uint8_t oldPhase, uint8_t newPhase);

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

        // IECBUS
        void atnChanged(bool atnLow) override;
        void clkChanged(bool clkState) override;
        void dataChanged(bool dataState) override;
        void setSRQAsserted(bool state) override;
        void forceSyncIEC() override;
        inline bool isSRQAsserted() const override { return srqAsserted; }

        // IECBUS communication
        void onListen() override;
        void onUnListen() override;
        void onTalk() override;
        void onUnTalk() override;
        void onSecondaryAddress(uint8_t sa) override;

        // Drive Runtime Properties
        inline bool isGCRMode() const { return mediaPath == MediaPath::GCR_1541; }
        inline bool isTrack0() { return currentTrack == 0; }
        inline bool isIecTalking() const { return iecTalking; }
        inline bool isIecListening() const { return iecListening; }
        inline bool isBusDriversEnabled() const { return busDriversEnabled; }
        void setDensityCode(uint8_t code);
        void setHeadSide(bool side);
        void setBusDriversEnabled(bool output);
        void setBurstClock2MHz(bool enable);
        void syncTrackFromFDC();
        bool getByteReadyLow() const;

        // ML Monitor
        inline bool hasCIA() const override { return true; }
        inline bool hasVIA1() const override { return true; }
        inline bool hasVIA2() const override { return true; }
        inline bool hasFDC() const override  { return true; }
        inline const CPU* getDriveCPU() const override { return &driveCPU; }
        inline CPU* getDriveCPU() override { return &driveCPU; }
        inline const D1571Memory* getMemory() const override { return &d1571mem; }
        inline D1571Memory* getMemory() override { return &d1571mem; }
        inline const FDC177x* getFDC() const override { return &d1571mem.getFDC(); }
        inline const DriveCIA* getCIA() const override { return &d1571mem.getCIA(); }
        inline const D1571VIA* getVIA1() const override { return &d1571mem.getVIA1(); }
        inline const D1571VIA* getVIA2() const override { return &d1571mem.getVIA2(); }
        inline DriveStatus getDriveStatus() const override { return status; }
        inline const char* getDriveTypeName() const noexcept override { return "1571"; }
        IECSnapshot snapshotIEC() const override;

    protected:
        bool motorOn;

    private:

        // Drive chips
        CPU driveCPU;
        D1571Memory d1571mem;
        IRQLine IRQ;

        static constexpr uint8_t GCR5[16] =
        {
            0x0A, 0x0B, 0x12, 0x13,
            0x0E, 0x0F, 0x16, 0x17,
            0x09, 0x19, 0x1A, 0x1B,
            0x0D, 0x1D, 0x1E, 0x15
        };

        // Floppy factory
        std::unique_ptr<Disk> diskImage;

        enum class MediaPath { FDC_MFM, GCR_1541 };
        MediaPath mediaPath;

        // IECBUS
        bool atnLineLow;
        bool clkLineLow;
        bool dataLineLow;
        bool srqAsserted;
        bool iecListening;
        bool iecTalking;
        bool presenceAckDone;
        bool expectingSecAddr;
        bool expectingDataByte;
        uint8_t currentListenSA;
        uint8_t currentTalkSA;

        // Drive Properties
        bool currentSide;
        bool busDriversEnabled;
        bool twoMHzMode;
        uint8_t densityCode; // 0..3

        // IEC listener data RX (C64 -> drive, ATN high, drive listening)
        bool iecRxActive;
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
        int halfTrackPos;
        uint8_t currentTrack;
        uint8_t currentSector;

        // GCR
        std::vector<uint8_t> gcrTrackStream;
        std::vector<uint8_t> gcrSync;
        int  gcrBitCounter; // Used to rate limit bits
        size_t gcrPos;
        bool gcrDirty;

        bool gcrTick();
        void gcrAdvance(uint32_t dc);
        void rebuildGCRTrackStream();
        void gcrEncode4Bytes(const uint8_t in[4], uint8_t out[5]);
        void gcrEncodeBytes(const uint8_t* in, size_t len, std::vector<uint8_t>& out);
        int sectorsPerTrack1541(int track1based);

        // Helper
        inline int stepIndex(uint8_t p) const { return (p & 0x03) * 2; }
};

#endif // D1571_H
