// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Drive/D1541.h"

D1541::D1541(int deviceNumber) :
    // Power on defaults
    motorOn(false),
    diskLoaded(false),
    SRQAsserted(false),
    lastError(DriveError::NONE),
    status(DriveStatus::IDLE),
    currentTrack(0),
    currentSector(1)
{
   setDeviceNumber(deviceNumber);
}

D1541::~D1541() = default;

bool D1541::canMount(DiskFormat fmt) const
{
    return fmt == DiskFormat::D64;
}

void D1541::tick()
{
    d1541mem.tick();
    driveCPU.tick();
}

bool D1541::initialize(const std::string& loRom, const std::string& hiRom)
{

}

void D1541::reset()
{
    motorOn = false;
    loadedDiskName.clear();
    diskLoaded = false;
    SRQAsserted = false;
    lastError = DriveError::NONE;
    status = DriveStatus::IDLE;
    currentTrack = 0;
    currentSector = 1;
    d1541mem.reset();
    driveCPU.attachMemoryInstance(&d1541mem);
    driveCPU.attachIRQLineInstance(d1541mem.getIRQLine());
    d1541mem.getVIA1().attachPeripheralInstance(this, D1541VIA::VIARole::VIA1_DataHandler);
    d1541mem.getVIA2().attachPeripheralInstance(this, D1541VIA::VIARole::VIA2_AtnMonitor);
    driveCPU.reset();
}

void D1541::loadDisk(const std::string& path)
{
    diskImage = DiskFactory::create(path);
    if (diskImage->loadDisk(path))
    {
        // Extract and store only the filename part
        loadedDiskName = path.substr(path.find_last_of("/\\") + 1);
        diskLoaded = true;
        status = DriveStatus::READY;
        currentTrack = 18;  // Directory track by default
        currentSector = 0;  // First sector of directory
    }
    else
    {
        diskLoaded = false;
        loadedDiskName.clear();
        status = DriveStatus::IDLE;
        lastError = DriveError::NO_DISK;
        throw std::runtime_error("Failed to load disk: " + path);
    }
}

void D1541::unloadDisk()
{
    diskImage.reset();  // Reset disk image by assigning a fresh instance
    diskLoaded = false;
    loadedDiskName.clear();
    currentTrack = 0;
    currentSector = 1;
    status = DriveStatus::IDLE;
}

bool D1541::isDiskLoaded() const
{
    return diskLoaded;
}

const std::string& D1541::getLoadedDiskName() const
{
    return loadedDiskName;
}

uint8_t D1541::getCurrentTrack() const
{
    return currentTrack;
}

uint8_t D1541::getCurrentSector() const
{
    return currentSector;
}

void D1541::startMotor()
{
    if (!motorOn)
    {
        motorOn = true;
    }
}

void D1541::stopMotor()
{
    motorOn = false;
}

bool D1541::isMotorOn() const
{
    return motorOn;
}

void D1541::clkChanged(bool clkState)
{
    if (bus)
    {
        bus->setClkLine(clkState);
    }
}

void D1541::dataChanged(bool dataState)
{
    if (bus)
    {
        bus->setDataLine(dataState);
    }
}

bool D1541::isSRQAsserted() const
{

    return SRQAsserted;
}

void D1541::setSRQAsserted(bool state)
{
    SRQAsserted = state;
    if (bus)
    {
        bus->setSrqLine(state);
    }
}

std::vector<uint8_t> D1541::readSector(uint8_t track, uint8_t sector)
{
    if (!diskLoaded)
    {
        throw std::runtime_error("Attempted read from disk, but no disk loaded.");
    }

    return diskImage->readSector(track, sector);
}

bool D1541::writeSector(uint8_t track, uint8_t sector, const std::vector<uint8_t>& data)
{
    if (!diskLoaded)
    {
        throw std::runtime_error("Attempted write to disk, but no disk loaded.");
    }

    return diskImage->writeSector(track, sector, data);
}

bool D1541::formatDisk(const std::string& volumeName, const std::string& volumeID)
{
    if (!diskLoaded)
    {
        throw std::runtime_error("Attempted to format disk, but no disk loaded.");
    }

    return diskImage->formatDisk(volumeName, volumeID);
}

bool D1541::writeFile(const std::string& fileName, const std::vector<uint8_t>& fileData)
{
    if (!diskLoaded)
    {
        throw std::runtime_error("Attempted to write file, but no disk loaded.");
    }

    return diskImage->writeFile(fileName, fileData);
}

bool D1541::deleteFile(const std::string& fileName)
{
    if (!diskLoaded)
    {
        throw std::runtime_error("Attempted to delete file, but no disk loaded.");
    }

    return diskImage->deleteFile(fileName);
}

bool D1541::renameFile(const std::string& oldName, const std::string& newName)
{
    if (!diskLoaded)
    {
        throw std::runtime_error("Attepmted to rename file, but no disk loaded.");
    }

    return diskImage->renameFile(oldName, newName);
}

bool D1541::copyFile(const std::string& srcName, const std::string& destName)
{
    if (!diskLoaded)
    {
        throw std::runtime_error("Attempted to copy file, but no disk loaded.");
    }

    return diskImage->copyFile(srcName, destName);
}

std::vector<uint8_t> D1541::getDirectoryListing()
{
    if (!diskLoaded)
    {
        throw std::runtime_error("Attempted to get directory listing, but no disk loaded.");
    }

    return diskImage->getDirectoryListing();
}

std::vector<uint8_t> D1541::loadFileByName(const std::string& name)
{
    if (!diskLoaded)
    {
        throw std::runtime_error("Attempted to load file by name, but no disk loaded.");
    }

    std::vector<uint8_t> data = diskImage->loadFileByName(name);
    if (!data.empty())
    {
        data.insert(data.begin(), 0x08); // MSB
        data.insert(data.begin(), 0x01); // LSB
    }

    return data;
}

void D1541::processListenBuffer()
{
    std::string cmd(listenBuffer.begin(), listenBuffer.end());

    if (cmd == "0:$")
    {
        // turn a real directory listing into bytes
        auto dir = getDirectoryListing();
        for (auto b : dir) talkQueue.push(b);
        talkQueue.push(0x0D);            // CR
    }
    else if (cmd.rfind("1:",0) == 0) // “1:<filename>”
    {
        auto payload = loadFileByName(cmd.substr(2));
        for (auto b : payload) talkQueue.push(b);
        talkQueue.push(0x0D);
    }
    listenBuffer.clear();
}
