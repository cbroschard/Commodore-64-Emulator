// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include <algorithm>
#include "Floppy/NIB.h"
#include "GCR/GCRTrackStream.h"

static size_t findTrackCycle(const uint8_t* data, size_t captureLength, size_t expectedLength)
{
    constexpr size_t searchWindow = 512;
    constexpr size_t matchLength  = 64;

    if (!data)
        return 0;

    if (captureLength < matchLength)
        return 0;

    if (expectedLength >= captureLength)
        return 0;

    const size_t minLength = (expectedLength > searchWindow) ? expectedLength - searchWindow : 1;
    const size_t maxCandidate = captureLength - matchLength;
    const size_t maxLength = std::min(expectedLength + searchWindow, maxCandidate);

    if (minLength > maxLength)
        return 0;

    size_t bestLength = 0;
    size_t bestScore  = 0;

    for (size_t candidate = minLength; candidate <= maxLength; ++candidate)
    {
        size_t score = 0;

        for (size_t i = 0; i < matchLength; ++i)
        {
            if (data[i] == data[candidate + i])
                ++score;
        }

        if (score > bestScore)
        {
            bestScore  = score;
            bestLength = candidate;
        }
    }

    if (bestScore < (matchLength / 2))
        return 0;

    return bestLength;
}

NIB::NIB()
{

}

NIB::~NIB()
{

}

bool NIB::loadDisk(const std::string& filePath)
{
    if (!loadDiskImage(filePath))
        return false;

    if (!parseHeaderAndTracks())
    {
        tracks.clear();
        return false;
    }

    writeProtected = true;
    dirty = false;

    return true;
}

bool NIB::saveDisk(const std::string& filePath)
{
    (void)filePath;
    return false;
}

bool NIB::readRawTrack(size_t halfTrack, GCRTrackStream& outTrack) const
{
    outTrack.clear();

    if (!hasRawTrack(halfTrack))
        return false;

    const NIBTrack& track = tracks[halfTrack];

    if (track.data.empty())
        return false;

    outTrack.setTrackData(track.data);

    if (!track.speedZones.empty())
        outTrack.setSpeedZones(track.speedZones);

    return true;
}

bool NIB::writeRawTrack(size_t halfTrack, const GCRTrackStream& track)
{
    (void)halfTrack;
    (void)track;
    return false;
}

const std::vector<uint8_t>& NIB::getRawImage() const
{
        return fileImageBuffer;
}

bool NIB::validateDiskImage()
{
    static constexpr char signature[] = "MNIB-1541-RAW";
    constexpr size_t signatureLength = sizeof(signature) - 1;

    if (fileImageBuffer.size() < NIB_HEADER_SIZE + NIB_TRACK_LENGTH)
        return false;

    if (!std::equal(signature, signature + signatureLength, fileImageBuffer.begin()))
        return false;

    const size_t versionOffset = signatureLength;

    if (versionOffset >= NIB_HEADER_SIZE)
        return false;

    const uint8_t version = fileImageBuffer[versionOffset];

    if (version != 3)
        return false;

    return true;
}

bool NIB::parseHeaderAndTracks()
{
    if (!validateDiskImage())
        return false;

    tracks.clear();

    constexpr size_t trackTableOffset = 0x10;
    size_t dataOffset = NIB_HEADER_SIZE;

    auto nominalTrackLength = [](uint8_t density) -> size_t
    {
        switch (density & 0x03)
        {
            case 3: return 7692;
            case 2: return 7143;
            case 1: return 6667;
            case 0: return 6250;
        }

        return 7692;
    };

    for (size_t entry = 0; entry < 120; ++entry)
    {
        const size_t tableOffset = trackTableOffset + (entry * 2);

        if (tableOffset + 1 >= NIB_HEADER_SIZE)
            break;

        const uint8_t nibHalfTrack = fileImageBuffer[tableOffset];
        const uint8_t density = fileImageBuffer[tableOffset + 1];

        if (nibHalfTrack == 0)
            continue;

        if (nibHalfTrack < 2)
            return false;

        const size_t halfTrack = static_cast<size_t>(nibHalfTrack - 2);

        if (halfTrack >= tracks.size())
            tracks.resize(halfTrack + 1);

        if (dataOffset + NIB_TRACK_LENGTH > fileImageBuffer.size())
            return false;

        const uint8_t trackDensity = density & 0x03;
        const size_t expectedLength = nominalTrackLength(trackDensity);
        const uint8_t* rawTrack = fileImageBuffer.data() + dataOffset;
        size_t trackLength = findTrackCycle(rawTrack, NIB_TRACK_LENGTH, expectedLength);

        if (trackLength == 0)
            trackLength = expectedLength;

        if (trackLength > NIB_TRACK_LENGTH)
            trackLength = NIB_TRACK_LENGTH;

        NIBTrack& track = tracks[halfTrack];

        track.present = true;
        track.density = trackDensity;

        track.data.assign(rawTrack, rawTrack + trackLength);
        track.speedZones.assign(track.data.size(), trackDensity);

        dataOffset += NIB_TRACK_LENGTH;
    }

    return !tracks.empty();
}
