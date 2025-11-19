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

        // DOS operations
        std::vector<uint8_t> readSector(uint8_t track, uint8_t sector);
        bool writeSector(uint8_t track, uint8_t sector, const std::vector<uint8_t>& data);
        bool formatDisk(const std::string& volumeName, const std::string& volumeID);
        bool writeFile(const std::string& fileName, const std::vector<uint8_t>& fileData);
        bool deleteFile(const std::string& fileName);
        bool renameFile(const std::string& oldName, const std::string& newName);
        bool copyFile(const std::string& srcName, const std::string& destName);

        // File/Directory getters
        std::vector<uint8_t> getDirectoryListing() override;
        std::vector<uint8_t> loadFileByName(const std::string& name) override;

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

        // Listen buffer handling
        void processListenBuffer() override;
};

#endif // D1541_H
