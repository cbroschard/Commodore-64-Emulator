// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Cartridge/IDE64/IDE64ImageDevice.h"

IDE64ImageDevice::IDE64ImageDevice() :
    sectorCount_(0),
    sectorSize_(512),
    dirty_(false),
    readOnly_(false)
{

}

IDE64ImageDevice::~IDE64ImageDevice()
{

}

void IDE64ImageDevice::reset()
{
    sectorSize_ = 512;
}

void IDE64ImageDevice::flush()
{

}

bool IDE64ImageDevice::isPresent() const
{
    return (sectorSize_ > 0) &&
           (sectorCount_ > 0) &&
           (imageData.size() == static_cast<size_t>(sectorCount_) * sectorSize_);
}

bool IDE64ImageDevice::readSector(uint32_t lba, uint8_t* buffer, size_t size)
{
    return true;
}

bool IDE64ImageDevice::writeSector(uint32_t lba, const uint8_t* buffer, size_t size)
{
    return true;
}

IDE64BlockDevice::DeviceInfo IDE64ImageDevice::getDeviceInfo() const
{
    DeviceInfo info;
    info.present = isPresent();
    info.readOnly = isReadOnly();
    info.totalSectors = sectorCount();
    info.logicalSectorSize = sectorSize();

    info.serialNumber = serialNumber_;
    info.firmwareRevision = firmwareRevision_;
    info.modelNumber = modelNumber_;

    info.cylinders = cylinders_;
    info.heads = heads_;
    info.sectorsPerTrack = sectorsPerTrack_;

    return info;
}
