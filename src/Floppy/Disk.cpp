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
    if (!file.read(reinterpret_cast<char*>(fileImageBuffer.data()), size))
    {
        std::cerr << "Failed to read file: " << imagePath << std::endl;
        return false;
    }

    if (!validateDiskImage())
    {
        std::cerr << "Failed to validate the disk image, not a valid image!" << imagePath << std::endl;
        return false;
    }

    return true;
}
