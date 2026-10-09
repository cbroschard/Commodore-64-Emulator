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
        const uint8_t* rawTrack = fileImageBuffer.data() + dataOffset;
        const ExtractedTrack extracted = extractTrack(rawTrack, NIB_TRACK_LENGTH, trackDensity);

        if (!extracted.valid)
            return false;

        NIBTrack& track = tracks[halfTrack];

        track.present = true;
        track.density = trackDensity;

        track.data = extracted.data;
        track.speedZones = extracted.speedZones;

        dataOffset += NIB_TRACK_LENGTH;
    }

    return !tracks.empty();
}

NIB::NIBTrackCycle NIB::findTrackCycle(const uint8_t* data, size_t captureLength, size_t expectedLength) const
{
    constexpr size_t searchWindow = 512;
    constexpr size_t matchLength  = 32;

    if (!data)
        return {};

    if (captureLength < (matchLength * 2))
        return {};

    if (expectedLength >= captureLength)
        return {};

    const size_t minLength = (expectedLength > searchWindow) ? expectedLength - searchWindow : 1;

    const size_t maxLength = expectedLength + searchWindow;

    for (size_t start = 0; start + matchLength < captureLength; ++start)
    {
        const size_t minCandidate = start + minLength;

        if (minCandidate + matchLength > captureLength)
            break;

        const size_t maxCandidate = std::min(start + maxLength, captureLength - matchLength);

        if (minCandidate > maxCandidate)
            continue;

        for (size_t candidate = minCandidate; candidate <= maxCandidate; ++candidate)
        {
            if (std::equal(data + start, data + start + matchLength, data + candidate))
            {
                NIBTrackCycle result;

                result.start  = start;
                result.length = candidate - start;
                result.found  = true;

                return result;
            }
        }
    }

    return {};
}

NIB::ExtractedTrack NIB::extractTrack(const uint8_t* rawTrack, size_t captureLength, uint8_t density) const
{
    ExtractedTrack result;

    if (!rawTrack)
        return result;

    if (captureLength == 0)
        return result;

    size_t expectedLength = 7692;

    switch (density & 0x03)
    {
        case 3: expectedLength = 7692; break;
        case 2: expectedLength = 7143; break;
        case 1: expectedLength = 6667; break;
        case 0: expectedLength = 6250; break;
    }

    const NIBTrackCycle cycle = findTrackCycle(rawTrack, captureLength, expectedLength);

    size_t trackStart = 0;
    size_t trackLength = expectedLength;

    if (cycle.found)
    {
        trackStart  = cycle.start;
        trackLength = cycle.length;
    }

    if (trackStart >= captureLength)
        trackStart = 0;

    if (trackLength > captureLength - trackStart)
        trackLength = captureLength - trackStart;

    if (trackLength == 0)
        return result;

    result.data.assign(rawTrack + trackStart, rawTrack + trackStart + trackLength);
    result.speedZones.assign(result.data.size(), density & 0x03);
    result.start  = trackStart;
    result.length = trackLength;
    result.valid  = true;

    return result;
}
