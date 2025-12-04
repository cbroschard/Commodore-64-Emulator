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

        // Motor control
        inline void startMotor() override { motorOn = true;  }
        inline void stopMotor()  override { motorOn = false; }
        inline bool isMotorOn() const override { return motorOn; }

        // IEC
        inline bool isSRQAsserted() const override { return SRQAsserted; }
        inline void setSRQAsserted(bool state) override { SRQAsserted = state; }
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
        const char* getDriveTypeName() const noexcept override { return "1541"; }

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

        // SRQ status
        bool SRQAsserted;

        // Drive geometry
        int     halfTrackPos;
        uint8_t currentTrack;
        uint8_t currentSector;
};

#endif // D1541_H
