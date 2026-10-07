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

        NIBTrack& track = tracks[halfTrack];

        track.present = true;

        track.data.assign(fileImageBuffer.begin() + dataOffset, fileImageBuffer.begin() + dataOffset + NIB_TRACK_LENGTH);
        track.speedZones.assign(track.data.size(), density & 0x03);
        dataOffset += NIB_TRACK_LENGTH;
    }

    return !tracks.empty();
}
