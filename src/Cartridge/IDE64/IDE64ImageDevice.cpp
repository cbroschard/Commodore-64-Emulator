// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include <cstring>
#include <fstream>
#include <utility>
#include "Cartridge/IDE64/IDE64ImageDevice.h"

IDE64ImageDevice::IDE64ImageDevice() :
    sectorCount_(0),
    sectorSize_(512),
    dirty_(false),
    readOnly_(false),
    serialNumber_("IDE6400001"),
    firmwareRevision_("1.0"),
    modelNumber_("IDE64 Image Device"),
    cylinders_(0),
    heads_(16),
    sectorsPerTrack_(63)
{

}

IDE64ImageDevice::~IDE64ImageDevice()
{

}

void IDE64ImageDevice::reset()
{
    sectorSize_ = 512;
}

bool IDE64ImageDevice::flush()
{
    if (!dirty_)
        return true;

    if (readOnly_)
        return false;

    if (backingPath_.empty())
        return false;

    return saveImage(backingPath_);
}

bool IDE64ImageDevice::isPresent() const
{
    return (sectorSize_ > 0) &&
           (sectorCount_ > 0) &&
           (imageData.size() == static_cast<size_t>(sectorCount_) * sectorSize_);
}

bool IDE64ImageDevice::createImage(uint32_t sectors, uint8_t fillValue)
{
    if (sectors == 0)
        return false;

    sectorSize_ = 512;

    imageData.assign(static_cast<size_t>(sectors) * sectorSize_, fillValue);

    sectorCount_ = sectors;
    readOnly_    = false;
    dirty_       = true;

    backingPath_.clear();

    updateDeviceInfo();

    return true;
}

bool IDE64ImageDevice::loadImage(const std::string& path, bool readOnly)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        return false;

    const std::streampos fileSize = file.tellg();

    if (fileSize <= 0)
        return false;

    if ((fileSize % 512) != 0)
        return false;

    const size_t size = static_cast<size_t>(fileSize);

    std::vector<uint8_t> newImage(size);

    file.seekg(0, std::ios::beg);

    if (!file.read(reinterpret_cast<char*>(newImage.data()),
                   static_cast<std::streamsize>(newImage.size())))
    {
        return false;
    }

    imageData = std::move(newImage);

    sectorSize_  = 512;
    sectorCount_ = static_cast<uint32_t>(imageData.size() / sectorSize_);
    readOnly_    = readOnly;
    dirty_       = false;
    backingPath_ = path;

    updateDeviceInfo();

    return true;
}

bool IDE64ImageDevice::saveImage(const std::string& path)
{
    if (!isPresent())
        return false;

    std::ofstream file(path, std::ios::binary | std::ios::trunc);

    if (!file.is_open())
        return false;

    file.write(reinterpret_cast<const char*>(imageData.data()), static_cast<std::streamsize>(imageData.size()));

    if (!file)
        return false;

    dirty_          = false;
    backingPath_    = path;

    return true;
}

void IDE64ImageDevice::clear()
{
    imageData.clear();

    dirty_          = false;
    readOnly_       = false;

    sectorSize_     = 512;
    sectorCount_    = 0;

    cylinders_ = 0;
    heads_ = 16;
    sectorsPerTrack_ = 63;

    backingPath_.clear();
    updateDeviceInfo();
}

bool IDE64ImageDevice::readSector(uint32_t lba, uint8_t* buffer, size_t size)
{
    if (!buffer)
        return false;

    if (size != sectorSize_)
        return false;

    if (!isPresent())
        return false;

    if (lba >= sectorCount_)
        return false;

    const size_t offset = static_cast<size_t>(lba) * sectorSize_;
    std::memcpy(buffer, imageData.data() + offset, size);
    return true;
}

bool IDE64ImageDevice::writeSector(uint32_t lba, const uint8_t* buffer, size_t size)
{
    if (!buffer)
        return false;

    if (readOnly_)
        return false;

    if (size != sectorSize_)
        return false;

    if (!isPresent())
        return false;

    if (lba >= sectorCount_)
        return false;

    const size_t offset = static_cast<size_t>(lba) * sectorSize_;
    std::memcpy(imageData.data() + offset, buffer, size);
    dirty_ = true;
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

void IDE64ImageDevice::updateDeviceInfo()
{
    heads_ = 16;
    sectorsPerTrack_ = 63;

    if (sectorCount_ == 0)
    {
        cylinders_ = 0;
        return;
    }

    const uint32_t sectorsPerCylinder =
        static_cast<uint32_t>(heads_) * sectorsPerTrack_;

    uint32_t cylinders = sectorCount_ / sectorsPerCylinder;

    if (cylinders == 0)
        cylinders = 1;

    if (cylinders > 0xFFFF)
        cylinders = 0xFFFF;

    cylinders_ = static_cast<uint16_t>(cylinders);
}
