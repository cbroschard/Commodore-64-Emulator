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

struct Geometry
{
    std::vector<int> sectorsPerTrack;
    std::vector<size_t> trackOffsets;
    bool hasPerSectorCRC = false;
};

struct TrackSector
{
    uint8_t track;
    uint8_t sector;
};

class CBMImage : public Disk
{
    public:
        CBMImage();
        virtual ~CBMImage();

        // Getters for File/Directory
        std::vector<uint8_t> getDirectoryListing();
        std::vector<uint8_t> loadFileByName(const std::string&);

        inline bool supportsSectorAccess() const override { return true; }

        // File operations
        bool writeFile(const std::string& fileName, const std::vector<uint8_t>& fileData);
        bool deleteFile(const std::string& fileName);
        bool renameFile(const std::string& oldName, const std::string& newName);
        bool copyFile(const std::string& srcName, const std::string& destName);

        // Reading/writing
        std::vector<uint8_t> readSector(uint8_t track, uint8_t sector);
        bool writeSector(uint8_t track, uint16_t sector, const std::vector<uint8_t>& data);

        // BAM Management and maintenance
        bool formatDisk(const std::string& volumeName, const std::string& volumeID);
        bool validateDirectory();

    protected:
        static constexpr size_t SECTOR_SIZE = 256;

        Geometry geom;

        virtual size_t sectorSize() const { return SECTOR_SIZE; }

        std::vector<TrackSector> bamLocations;  // Handle different BAM locations based on image format
        TrackSector directoryStart;   // Handle different directory start locations based on image format

        size_t computeOffset(uint8_t track, uint8_t sector);

        // Disk format helpers
        virtual void initializeGeometryForBlankImage() = 0;
        virtual void initializeBlankImageBuffer() = 0;
        virtual bool writeBlankBAM(const std::string& volumeName, const std::string& volumeID) = 0;
        virtual bool writeBlankDirectory() = 0;

        // Helpers for image validation
        bool isValidPETSCII(uint8_t c);
        bool validateHeader();
        bool validateDiskNameAndID();
        bool validateDirectoryChain();
        bool validateDiskImage() override;

        virtual uint16_t getSectorsForTrack(uint8_t track) = 0;

        // BAM management
        virtual bool allocateSector(uint8_t& outTrack, uint8_t& outSector);
        virtual void freeSector(uint8_t track, uint8_t sector);

        // Helper to convert ASCII to PETSCII
        uint8_t asciiToPetscii(unsigned char asciiChar);
};

#endif // CBMIMAGE_H
