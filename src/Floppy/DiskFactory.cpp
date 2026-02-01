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

static std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return char(std::tolower(c)); });
    return s;
}

DiskFormat DiskFactory::detectFormat(const std::string &path)
{
    if (path.size() < 4) return DiskFormat::Unknown;
    std::string ext = lower(path.substr(path.size() - 4));

    if (ext == ".d64") return DiskFormat::D64;
    if (ext == ".d71") return DiskFormat::D71;
    if (ext == ".d81") return DiskFormat::D81;
    if (ext == ".g64") return DiskFormat::G64;

    return DiskFormat::Unknown;
}
