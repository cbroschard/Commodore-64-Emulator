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
        void tick() override;

        // Initialize everything
        bool initialize(const std::string& loRom, const std::string& hiRom);

        // Compatibility check
        bool canMount(DiskFormat fmt) const override;

        // Drive interface
        void loadDisk(const std::string& path) override;
        void unloadDisk() override;
        bool isDiskLoaded() const override;
        inline const std::string& getLoadedDiskName() const override { return loadedDiskName; }
        uint8_t getCurrentTrack() const override;
        uint8_t getCurrentSector() const override;

        // Motor control
        void startMotor() override;
        void stopMotor() override;
        bool isMotorOn() const override;

        // Peripheral interface
        void clkChanged(bool clkState)  override;
        void dataChanged(bool dataState) override;

        // SRQ getter
        bool isSRQAsserted() const override;

        // SRQ setter
        void setSRQAsserted(bool state) override;

        // ML Monitor
        inline bool hasCIA() const override { return false; }
        inline bool hasVIA1() const override { return true; }
        inline bool hasVIA2() const override { return true; }
        inline bool hasFDC() const override  { return false; }
        const char* getDriveTypeName() const noexcept override { return "1541"; }
        bool isDrive() const override { return true; }

    protected:
        bool motorOn;

    private:

        // Owning pointers
        D1541Memory d1541mem;
        CPU driveCPU;

        // Floppy Image
        std::string loadedDiskName;
        bool diskLoaded;

        // SRQ status
        bool SRQAsserted;

        // Status tracking
        DriveError lastError;
        DriveStatus status;

        // Drive geometry
        uint8_t currentTrack;
        uint8_t currentSector;
};

#endif // D1541_H
