// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include <fstream>
#include <iostream>
#include "Floppy/Disk.h"

Disk::Disk() = default;

Disk::~Disk() = default;

bool Disk::loadDiskImage(const std::string& imagePath)
{
    std::ifstream file(imagePath, std::ios::binary | std::ios::ate);

    if (!file.is_open())
    {
        std::cerr << "Failed to open file: " << imagePath << std::endl;
        return false;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    fileImageBuffer.resize(size);

    if (!file.read(
            reinterpret_cast<char*>(fileImageBuffer.data()),
            size))
    {
        std::cerr << "Failed to read file: " << imagePath << std::endl;
        return false;
    }

    file.close();

    if (!validateDiskImage())
    {
        std::cerr
            << "Failed to validate the disk image, not a valid image! "
            << imagePath << std::endl;
        return false;
    }

    //
    // Determine whether the backing image can actually be written.
    //
    // std::ios::in | std::ios::out does NOT truncate the file.
    //
    std::fstream writeTest(imagePath, std::ios::binary | std::ios::in | std::ios::out);

    writeProtected = !writeTest.is_open();

    if (writeTest.is_open())
        writeTest.close();

    return true;
}

bool Disk::hasRawTrack(size_t halfTrack) const
{
    (void)halfTrack;
    return false;
}

bool Disk::readRawTrack(size_t halfTrack, GCRTrackStream& outTrack) const
{
    (void)halfTrack;
    (void)outTrack;
    return false;
}

 bool Disk::writeRawTrack(size_t halfTrack, const GCRTrackStream& track)
 {
     (void)halfTrack;
     (void)track;
     return false;
 }
