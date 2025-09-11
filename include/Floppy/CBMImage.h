// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef CBMIMAGE_H
#define CBMIMAGE_H

#include "Disk.h"

struct TrackSector{uint8_t track, sector;};

class CBMImage : public Disk
{
    public:
        CBMImage();
        virtual ~CBMImage();

        // Getters for File/Directory
        std::vector<uint8_t> getDirectoryListing() override;
        std::vector<uint8_t> loadFileByName(const std::string&) override;

        // File operations
        bool writeFile(const std::string& fileName, const std::vector<uint8_t>& fileData) override;
        bool deleteFile(const std::string& fileName) override;
        bool renameFile(const std::string& oldName, const std::string& newName) override;
        bool copyFile(const std::string& srcName, const std::string& destName) override;

        // BAM Management and maintenance
        bool formatDisk(const std::string& volumeName, const std::string& volumeID) override;
        bool validateDirectory() override;

    protected:

        std::vector<TrackSector> bamLocations;  // Handle different BAM locations based on image format
        TrackSector directoryStart;   // Handle different directory start locations based on image format

        // Helpers for image validation
        bool isValidPETSCII(uint8_t c);
        bool validateHeader();
        bool validateDiskNameAndID();
        bool validateDirectoryChain();
        bool validateDiskImage() override;

        // Helper to convert ASCII to PETSCII
        uint8_t asciiToPetscii(unsigned char asciiChar);

    private:

        bool allocateSector(uint8_t& outTrack, uint8_t& outSector) override;
        void freeSector(uint8_t track, uint8_t sector) override;
};

#endif // CBMIMAGE_H
