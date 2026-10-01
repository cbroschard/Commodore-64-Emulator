// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include <fstream>
#include "Floppy/G64.h"

G64::G64() = default;

G64::~G64() = default;

bool G64::loadDisk(const std::string& filePath)
{
    return loadDiskImage(filePath);
}

bool G64::saveDisk(const std::string& filePath)
{
    if (fileImageBuffer.empty())
        return false;

    std::ofstream out(filePath, std::ios::binary);

    if (!out.is_open())
        return false;

    out.write(reinterpret_cast<const char*>(fileImageBuffer.data()), static_cast<std::streamsize>(fileImageBuffer.size()));

    return out.good();
}

size_t G64::getTrackCount() const
{
    return tracks.size();
}

bool G64::hasTrack(size_t index) const
{
    return index < tracks.size() && tracks[index].present;
}

const std::vector<uint8_t>& G64::getTrackData(size_t index) const
{
    static const std::vector<uint8_t> empty;

    if (!hasTrack(index))
        return empty;

    return tracks[index].data;
}

const std::vector<uint8_t>& G64::getTrackSpeedZones(size_t index) const
{
    static const std::vector<uint8_t> empty;

    if (!hasTrack(index))
        return empty;

    return tracks[index].speedZones;
}

const std::vector<uint8_t>& G64::getRawImage() const
{
    return fileImageBuffer;
}

bool G64::validateDiskImage()
{
    // Temporary until we implement the real G64 parser/validator.
    return !fileImageBuffer.empty();
}
