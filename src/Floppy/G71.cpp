// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include <fstream>
#include "Floppy/G71.h"
#include "GCR/GCRTrackStream.h"

G71::G71() = default;

G71::~G71() = default;

bool G71::loadDisk(const std::string& filePath)
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

bool G71::saveDisk(const std::string& filePath)
{
    if (fileImageBuffer.empty())
        return false;

    if (tracks.size() != header.trackOffsets.size())
        return false;

    for (size_t i = 0; i < tracks.size(); ++i)
    {
        if (!tracks[i].present)
            continue;

        const uint32_t offset = header.trackOffsets[i];

        if (offset == 0)
            continue;

        if (offset + 2 > fileImageBuffer.size())
            return false;

        const uint16_t storedLength = readLE16(fileImageBuffer, offset);

        if (tracks[i].data.size() != storedLength)
            return false;

        const size_t dataOffset = static_cast<size_t>(offset) + 2;

        if (dataOffset + storedLength > fileImageBuffer.size())
            return false;

        std::copy(tracks[i].data.begin(), tracks[i].data.end(), fileImageBuffer.begin() + dataOffset);
    }

    std::ofstream out(filePath, std::ios::binary | std::ios::trunc);

    if (!out)
        return false;

    out.write(reinterpret_cast<const char*>(fileImageBuffer.data()), static_cast<std::streamsize>(fileImageBuffer.size()));

    if (!out)
        return false;

    dirty = false;

    return true;
}

bool G71::readRawTrack(size_t halfTrack, GCRTrackStream& outTrack) const
{
    if (!hasTrack(halfTrack))
        return false;

    outTrack.clear();

    outTrack.setTrackData(
        getTrackData(halfTrack));

    outTrack.setSpeedZones(
        getTrackSpeedZones(halfTrack));

    return true;
}

bool G71::writeRawTrack(size_t halfTrack, const GCRTrackStream& track)
{
    return setTrackData(halfTrack, track.getTrackData());
}

size_t G71::getTrackCount() const
{
    return tracks.size();
}

bool G71::hasTrack(size_t index) const
{
    return index < tracks.size() && tracks[index].present;
}

const std::vector<uint8_t>& G71::getTrackData(size_t index) const
{
    static const std::vector<uint8_t> empty;

    if (!hasTrack(index))
        return empty;

    return tracks[index].data;
}

const std::vector<uint8_t>& G71::getTrackSpeedZones(size_t index) const
{
    static const std::vector<uint8_t> empty;

    if (!hasTrack(index))
        return empty;

    return tracks[index].speedZones;
}

bool G71::setTrackData(size_t index, const std::vector<uint8_t>& data)
{
    if (index >= tracks.size())
        return false;

    if (!tracks[index].present)
        return false;

    if (data.size() != tracks[index].data.size())
        return false;

    tracks[index].data = data;
    dirty = true;

    return true;
}

const std::vector<uint8_t>& G71::getRawImage() const
{
    return fileImageBuffer;
}

bool G71::validateDiskImage()
{
    return parseHeader();
}

uint16_t G71::readLE16(const std::vector<uint8_t>& data, size_t offset)
{
    return static_cast<uint16_t>(static_cast<uint16_t>(data[offset]) | (static_cast<uint16_t>(data[offset + 1]) << 8));
}

uint32_t G71::readLE32(const std::vector<uint8_t>& data, size_t offset)
{
    return static_cast<uint32_t>(data[offset]) |
           (static_cast<uint32_t>(data[offset + 1]) << 8) |
           (static_cast<uint32_t>(data[offset + 2]) << 16) |
           (static_cast<uint32_t>(data[offset + 3]) << 24);
}

bool G71::parseHeader()
{
    static constexpr char signature[] = "GCR-1571";
    constexpr size_t signatureLength = sizeof(signature) - 1;

    if (fileImageBuffer.size() < 12)
        return false;

    if (!std::equal(signature, signature + signatureLength, fileImageBuffer.begin()))
        return false;

    header.version = fileImageBuffer[8];

    if (header.version != 0)
        return false;

    header.trackCount = fileImageBuffer[9];

    if (header.trackCount == 0)
        return false;

    header.maxTrackSize = readLE16(fileImageBuffer, 10);

    const size_t trackTableOffset = 12;
    const size_t speedTableOffset = trackTableOffset + (static_cast<size_t>(header.trackCount) * 4);
    const size_t headerEnd = speedTableOffset + (static_cast<size_t>(header.trackCount) * 4);

    if (headerEnd > fileImageBuffer.size())
        return false;

    header.trackOffsets.resize(header.trackCount);
    header.speedEntries.resize(header.trackCount);

    for (size_t i = 0; i < header.trackCount; ++i)
    {
        header.trackOffsets[i] = readLE32(fileImageBuffer, trackTableOffset + (i * 4));
        header.speedEntries[i] = readLE32(fileImageBuffer, speedTableOffset + (i * 4));
    }

    return true;
}

bool G71::parseTracks()
{
    tracks.clear();
    tracks.resize(header.trackCount);

    for (size_t i = 0; i < header.trackCount; ++i)
    {
        const uint32_t trackOffset = header.trackOffsets[i];

        if (trackOffset == 0)
            continue;

        if (trackOffset + 2 > fileImageBuffer.size())
            return false;

        const uint16_t trackLength = readLE16(fileImageBuffer, trackOffset);

        if (trackLength == 0)
            continue;

        const size_t dataOffset = static_cast<size_t>(trackOffset) + 2;

        if (dataOffset + trackLength > fileImageBuffer.size())
            return false;

        G71Track& track = tracks[i];

        track.data.assign(fileImageBuffer.begin() + dataOffset, fileImageBuffer.begin() + dataOffset + trackLength);

        track.speedZones.clear();

        const uint32_t speedEntry = header.speedEntries[i];

        if (speedEntry <= 3)
            track.speedZones.assign(track.data.size(), static_cast<uint8_t>(speedEntry));
        else
        {
            const size_t speedOffset = static_cast<size_t>(speedEntry);
            const size_t speedBytes = (track.data.size() + 3) / 4;

            if (speedOffset + speedBytes > fileImageBuffer.size())
                return false;

            track.speedZones.reserve(track.data.size());

            for (size_t byteIndex = 0; byteIndex < speedBytes; ++byteIndex)
            {
                const uint8_t packed = fileImageBuffer[speedOffset + byteIndex];

                for (int shift = 0; shift < 8 && track.speedZones.size() < track.data.size(); shift += 2)
                    track.speedZones.push_back(static_cast<uint8_t>((packed >> shift) & 0x03));
            }
        }

        track.present = true;
        dirty = false;
    }

    return true;
}
