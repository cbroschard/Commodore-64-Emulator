// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef DISK_H
#define DISK_H

#include <bit>
#include <cstdint>
#include <string>
#include <vector>
#include <fstream>
#include <string>
#include <iostream>
#include <set>
#include <algorithm>

struct Geometry
{
    std::vector<int> sectorsPerTrack;
    std::vector<size_t> trackOffsets;
    bool hasPerSectorCRC = false;
};

class Disk
{
    public:
        Disk();
        virtual ~Disk();

        // Loading/saving
        virtual bool loadDisk(const std::string& filePath) = 0;
        virtual bool saveDisk(const std::string& filePath) = 0;

        // Getters for File/Directory
        virtual std::vector<uint8_t> getDirectoryListing() = 0;
        virtual std::vector<uint8_t> loadFileByName(const std::string&) = 0;

        // File operations
        virtual bool writeFile(const std::string& fileName, const std::vector<uint8_t>& fileData) = 0;
        virtual bool deleteFile(const std::string& fileName) = 0;
        virtual bool renameFile(const std::string& oldName, const std::string& newName) = 0;
        virtual bool copyFile(const std::string& srcName, const std::string& destName) = 0;

        // Reading/writing
        std::vector<uint8_t> readSector(uint8_t track, uint8_t sector);
        bool writeSector(uint8_t track, uint16_t sector, const std::vector<uint8_t>& data);

        // BAM Management and maintenance
        virtual bool formatDisk(const std::string& volumeName, const std::string& volumeID) = 0;
        virtual bool validateDirectory() = 0;

    protected:

        Geometry geom;
        std::vector<uint8_t> fileImageBuffer; // Vector to hold file image data
        static constexpr size_t SECTOR_SIZE = 256;
        virtual size_t sectorSize() const { return SECTOR_SIZE; }
        size_t computeOffset(uint8_t track, uint8_t sector);

        // Disk image management
        bool loadDiskImage(const std::string& imagePath);
        virtual const std::vector<uint8_t>& getRawImage() const = 0;

        // Helpers
        virtual uint16_t getSectorsForTrack(uint8_t track) = 0;
        virtual bool validateDiskImage() = 0;

        // BAM management
        virtual bool allocateSector(uint8_t& outTrack, uint8_t& outSector) = 0;
        virtual void freeSector(uint8_t track, uint8_t sector) = 0;

    private:

};

#endif // DISK_H
