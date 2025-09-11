// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "DiskFactory.h"

DiskFactory::DiskFactory() = default;

DiskFactory::~DiskFactory() = default;

DiskFormat DiskFactory::detectFormat(const std::string &path)
{
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".d64")
    {
        return DiskFormat::D64;
    }
    else if (path.size() >= 4 && path.substr(path.size() -4) == ".d71")
    {
        return DiskFormat::D71;
    }
    else if(path.size() >= 4 && path.substr(path.size() -4) == ".d81")
    {
        return DiskFormat::D81;
    }
    else if(path.size() >= 4 && path.substr(path.size() -4) == ".g64")
    {
        return DiskFormat::G64;
    }
    return DiskFormat::Unknown;
}
