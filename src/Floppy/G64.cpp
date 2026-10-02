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
    header = {};
    tracks.clear();

    if (!loadDiskImage(filePath))
        return false;

    if (!validateDiskImage())
    {
        fileImageBuffer.clear();
        return false;
    }

    if (!parseTracks())
    {
        fileImageBuffer.clear();
        tracks.clear();
        return false;
    }

    dirty = false;
    return true;
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
    return parseHeader();
}

uint16_t G64::readLE16(const std::vector<uint8_t>& data, size_t offset)
{
    return static_cast<uint16_t>(static_cast<uint16_t>(data[offset]) | (static_cast<uint16_t>(data[offset + 1]) << 8));
}

uint32_t G64::readLE32(const std::vector<uint8_t>& data, size_t offset)
{
    return static_cast<uint32_t>(data[offset]) |
           (static_cast<uint32_t>(data[offset + 1]) << 8) |
           (static_cast<uint32_t>(data[offset + 2]) << 16) |
           (static_cast<uint32_t>(data[offset + 3]) << 24);
}

bool G64::parseHeader()
{
    static constexpr char signature[] = "GCR-1541";

    if (fileImageBuffer.size() < 12)
        return false;

    for (size_t i = 0; i < 8; ++i)
    {
        if (fileImageBuffer[i] != static_cast<uint8_t>(signature[i]))
            return false;
    }

    header.version      = fileImageBuffer[8];
    header.trackCount   = fileImageBuffer[9];
    header.maxTrackSize = readLE16(fileImageBuffer, 10);

    if (header.version != 0)
        return false;

    if (header.trackCount == 0)
        return false;

    const size_t trackTableOffset = 0x0C;

    const size_t speedTableOffset = trackTableOffset + static_cast<size_t>(header.trackCount) * 4;

    const size_t requiredSize = speedTableOffset + static_cast<size_t>(header.trackCount) * 4;

    if (requiredSize > fileImageBuffer.size())
        return false;

    header.trackOffsets.resize(header.trackCount);
    header.speedEntries.resize(header.trackCount);

    for (size_t i = 0; i < header.trackCount; ++i)
    {
        header.trackOffsets[i] = readLE32(fileImageBuffer, trackTableOffset + i * 4);
        header.speedEntries[i] = readLE32(fileImageBuffer, speedTableOffset + i * 4);
    }

    return true;
}

bool G64::parseTracks()
{
    tracks.clear();
    tracks.resize(header.trackCount);

    for (size_t i = 0; i < header.trackCount; ++i)
    {
        const uint32_t trackOffset = header.trackOffsets[i];
        const uint32_t speedEntry  = header.speedEntries[i];

        G64Track& track = tracks[i];

        // Offset 0 means this full/half-track is not present.
        if (trackOffset == 0)
        {
            track.present = false;
            continue;
        }

        // Need at least the 2-byte track length.
        if (trackOffset + 2 > fileImageBuffer.size())
            return false;

        const uint16_t trackLength = readLE16(fileImageBuffer, trackOffset);

        if (trackLength == 0)
            return false;

        // Optional sanity check against header maximum.
        if (header.maxTrackSize != 0 &&
            trackLength > header.maxTrackSize)
        {
            return false;
        }

        const size_t dataOffset = static_cast<size_t>(trackOffset) + 2;

        const size_t dataEnd = dataOffset + static_cast<size_t>(trackLength);

        if (dataEnd > fileImageBuffer.size())
            return false;

        track.data.assign(fileImageBuffer.begin() + dataOffset, fileImageBuffer.begin() + dataEnd);

        track.present = true;

        //
        // Speed information
        //
        track.speedZones.clear();

        if (speedEntry <= 3)
        {
            // Constant speed zone for the entire track.
            track.speedZones.assign(trackLength, static_cast<uint8_t>(speedEntry));
        }
        else
        {
            // speedEntry is an offset to packed 2-bit speed values.
            const size_t packedLength = (static_cast<size_t>(trackLength) + 3) / 4;

            const size_t speedOffset = static_cast<size_t>(speedEntry);

            if (speedOffset + packedLength > fileImageBuffer.size())
                return false;

            track.speedZones.resize(trackLength);

            for (size_t byteIndex = 0;
                 byteIndex < trackLength;
                 ++byteIndex)
            {
                const size_t packedIndex = byteIndex / 4;
                const size_t shift = (byteIndex % 4) * 2;

                const uint8_t packed = fileImageBuffer[speedOffset + packedIndex];

                track.speedZones[byteIndex] = static_cast<uint8_t>((packed >> shift) & 0x03);
            }
        }
    }

    return true;
}
