// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef IDE64IMAGEDEVICE_H
#define IDE64IMAGEDEVICE_H

#include <vector>
#include "Cartridge/IDE64/IDE64BlockDevice.h"

class IDE64ImageDevice : public IDE64BlockDevice
{
    public:
        IDE64ImageDevice();
        ~IDE64ImageDevice();

        void reset() override;
        void flush() override;

        inline uint32_t sectorCount() const override { return sectorCount_; }
        inline uint16_t sectorSize() const override { return sectorSize_; }

        inline bool isReadOnly() const override { return readOnly_; }
        bool isPresent() const override;

        bool readSector(uint32_t lba, uint8_t* buffer, size_t size) override;
        bool writeSector(uint32_t lba, const uint8_t* buffer, size_t size) override;

        DeviceInfo getDeviceInfo() const override;

    protected:

    private:
        std::vector<uint8_t> imageData;

        uint32_t sectorCount_;
        uint16_t sectorSize_;

        bool dirty_;
        bool readOnly_;

        std::string serialNumber_;
        std::string firmwareRevision_;
        std::string modelNumber_;

        uint16_t cylinders_;
        uint16_t heads_;
        uint16_t sectorsPerTrack_;
};

#endif // IDE64IMAGEDEVICE_H
