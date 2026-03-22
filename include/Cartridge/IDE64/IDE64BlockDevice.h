// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef IDE64BLOCKDEVICE_H
#define IDE64BLOCKDEVICE_H

#include <cstddef>
#include <cstdint>
#include <string>

class IDE64BlockDevice
{
    public:
        IDE64BlockDevice();
        virtual ~IDE64BlockDevice();

        struct DeviceInfo
        {
            bool present = false;
            bool readOnly = false;

            uint32_t totalSectors = 0;
            uint16_t logicalSectorSize = 512;

            uint16_t cylinders = 0;
            uint16_t heads = 0;
            uint16_t sectorsPerTrack = 0;

            std::string serialNumber;
            std::string firmwareRevision;
            std::string modelNumber;
        };

        virtual void reset() = 0;
        virtual void flush() = 0;

        virtual bool isReadOnly() const = 0;
        virtual bool isPresent() const = 0;

        virtual uint32_t sectorCount() const = 0;
        virtual uint16_t sectorSize() const = 0;

        virtual bool readSector(uint32_t lba, uint8_t* buffer, size_t size) = 0;
        virtual bool writeSector(uint32_t lba, const uint8_t* buffer, size_t size) = 0;

        virtual DeviceInfo getDeviceInfo() const = 0;

    protected:

    private:

};

#endif // IDE64BLOCKDEVICE_H
