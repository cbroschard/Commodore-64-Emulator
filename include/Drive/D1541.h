// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef D1541_H
#define D1541_H

#include "Drive.h"
#include "D1541Memory.h"
#include "D1541VIA.h"

class D1541 : public Drive
{
    public:
        D1541(int deviceNumber);
        virtual ~D1541();

        // Reset function
        void reset() override;

        // Advance drive via tick method
        void tick(uint32_t cycles) override;

        // Initialize everything
        bool initialize(const std::string& loRom, const std::string& hiRom);

        // Compatibility check
        inline bool canMount(DiskFormat fmt) const override { return fmt == DiskFormat::D64; }

        // Drive interface
        inline bool isDiskLoaded() const override { return diskLoaded; }
        inline const std::string& getLoadedDiskName() const override { return loadedDiskName; }
        inline uint8_t getCurrentTrack() const override { return currentTrack; }
        inline uint8_t getCurrentSector() const override { return currentSector; }
        void loadDisk(const std::string& path) override;
        void unloadDisk() override;

        // IECBUS communication
        void onListen() override;
        void onUnListen() override;
        void onTalk() override;
        void onUnTalk() override;
        void onSecondaryAddress(uint8_t sa) override;

        // Motor control
        inline void startMotor() override { motorOn = true;  }
        inline void stopMotor()  override { motorOn = false; }
        inline bool isMotorOn() const override { return motorOn; }

        // IEC
        inline bool isSRQAsserted() const override { return srqAsserted; }
        inline void setSRQAsserted(bool state) override { srqAsserted = state; }
        void clkChanged(bool clkState)  override;
        void dataChanged(bool dataState) override;

        // Status tracking
        DriveError  lastError;
        DriveStatus status;

        // ML Monitor
        inline bool hasCIA()  const override { return false; }
        inline bool hasVIA1() const override { return true;  }
        inline bool hasVIA2() const override { return true;  }
        inline bool hasFDC()  const override { return false; }
        inline bool isDrive() const override { return true;  }
        inline const CPU* getDriveCPU() const override { return &driveCPU; }
        inline CPU* getDriveCPU() override { return &driveCPU; }
        inline const D1541Memory* getMemory() const override { return &d1541mem; }
        inline D1541Memory* getMemory() override { return &d1541mem; }
        inline const D1541VIA* getVIA1() const override { return &d1541mem.getVIA1(); }
        inline const D1541VIA* getVIA2() const override { return &d1541mem.getVIA2(); }
        inline DriveStatus getDriveStatus() const override { return status; }
        const char* getDriveTypeName() const noexcept override { return "1541"; }
        IECSnapshot snapshotIEC() const override;

    protected:
        bool motorOn;

    private:

        // Owning pointers
        D1541Memory d1541mem;
        CPU driveCPU;

        // Floppy Image
        std::string loadedDiskName;
        bool        diskLoaded;
        bool        diskWriteProtected;

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

        // IEC listener data RX (C64 -> drive, ATN high, drive listening)
        bool iecRxActive;
        int iecRxBitCount;
        uint8_t iecRxByte;

        // Drive geometry
        int     halfTrackPos;
        uint8_t currentTrack;
        uint8_t currentSector;
};

#endif // D1541_H
