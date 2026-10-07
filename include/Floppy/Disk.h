// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef DISK_H
#define DISK_H

#include <cstdint>
#include <string>
#include <vector>

class Disk
{
    public:
        Disk();
        virtual ~Disk();

        enum class DiskTrackModel
        {
            Sector,
            RawGCR
        };

        // Loading/saving
        virtual bool loadDisk(const std::string& filePath) = 0;
        virtual bool saveDisk(const std::string& filePath) = 0;

        virtual DiskTrackModel getTrackModel() const = 0;

        bool isDirty() const { return dirty; }
        void clearDirty() { dirty = false; }

         // Backing-file access
        bool isWriteProtected() const { return writeProtected; }

    protected:
        bool dirty = false;
        bool writeProtected = false;

        std::vector<uint8_t> fileImageBuffer; // Vector to hold file image data

        // Disk image management
        bool loadDiskImage(const std::string& imagePath);
        virtual const std::vector<uint8_t>& getRawImage() const = 0;

        // Helpers
        virtual bool validateDiskImage() = 0;
};

#endif // DISK_H
