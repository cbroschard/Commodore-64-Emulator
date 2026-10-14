// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include <algorithm>
#include <iostream>
#include "Floppy/NIB.h"
#include "GCR/GCRCodec.h"
#include "GCR/GCRTrackStream.h"

NIB::NIB() = default;

NIB::~NIB() = default;

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

    if (version < 1 || version > 3)
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
    NIBTrackCycle result;

    constexpr size_t SEARCH_WINDOW = 256;
    constexpr size_t MIN_COMPARE_BYTES = 256;
    constexpr size_t MIN_MATCHING_RUN = 256;
    constexpr size_t MIN_MATCH_PERCENT = 30;

    if (!data || expectedLength == 0 || captureLength <= MIN_COMPARE_BYTES)
        return result;

    const size_t minLength = expectedLength > SEARCH_WINDOW ? expectedLength - SEARCH_WINDOW : 1;
    const size_t maxLength = std::min(expectedLength + SEARCH_WINDOW, captureLength - MIN_COMPARE_BYTES);

    if (minLength > maxLength)
        return result;

    size_t bestLength = 0;
    size_t bestRun = 0;
    size_t bestMatches = 0;
    size_t bestCompared = 0;

    for (size_t length = minLength;
         length <= maxLength;
         ++length)
    {
        const size_t compareBytes =
            captureLength - length;

        size_t matches = 0;
        size_t currentRun = 0;
        size_t longestRun = 0;

        for (size_t i = 0; i < compareBytes; ++i)
        {
            if (data[i] == data[i + length])
            {
                ++matches;
                ++currentRun;

                if (currentRun > longestRun)
                    longestRun = currentRun;
            }
            else
            {
                currentRun = 0;
            }
        }

        // Require a substantial matching region.
        if (longestRun < MIN_MATCHING_RUN)
            continue;

        // Reject candidates with very poor overall agreement.
        if (matches * 100 < compareBytes * MIN_MATCH_PERCENT)
            continue;

        // Prefer the longest continuous overlap.
        // Break ties using the overall matching percentage.
        const bool better = bestLength == 0 || longestRun > bestRun || (longestRun == bestRun && matches * bestCompared >
            bestMatches * compareBytes);

        if (better)
        {
            bestLength = length;
            bestRun = longestRun;
            bestMatches = matches;
            bestCompared = compareBytes;
        }
    }

    if (bestLength == 0)
        return result;

    result.start = 0;
    result.length = bestLength;
    result.found = true;

    return result;
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

    if (cycle.found)
    {
        // The NIB capture contains more than one revolution.
        // Compare the overlapping copies and choose a boundary
        // after any disagreement, when a reliable matching suffix exists.

        constexpr size_t MIN_MATCHING_SUFFIX = 32;

        const size_t overlap = captureLength - trackLength;
        size_t lastMismatch = overlap;

        for (size_t i = 0; i < overlap; ++i)
        {
            if (rawTrack[i] != rawTrack[i + trackLength])
                lastMismatch = i;
        }

        if (lastMismatch < overlap)
        {
            const size_t candidateStart = lastMismatch + 1;
            const size_t matchingSuffix = overlap - candidateStart;

            if (matchingSuffix >= MIN_MATCHING_SUFFIX && candidateStart + trackLength <= captureLength)
                trackStart = candidateStart;
        }
    }
    else
    {
        // Fallback when no reliable revolution overlap was found.
        // Keep the existing sector-header boundary approach.

        GCRCodec codec;

        constexpr size_t HEADER_BYTES = 10;
        constexpr size_t GAP_BYTES = 16;

        for (size_t pos = HEADER_BYTES; pos + HEADER_BYTES <= captureLength; ++pos)
        {
            std::vector<uint8_t> header;

            if (!codec.decodeBytes(rawTrack + pos, HEADER_BYTES, header))
                continue;

            if (header.size() != 8 || header[0] != 0x08)
                continue;

            const uint8_t sector = header[2];
            const uint8_t physicalTrack = header[3];

            if (physicalTrack < 1 || physicalTrack > 35)
                continue;

            if (sector >= codec.sectorsPerTrack1541(physicalTrack))
                continue;

            if (header[1] != static_cast<uint8_t>(
                    sector ^ physicalTrack ^ header[4] ^ header[5]))
                continue;

            const size_t candidateStart = pos > GAP_BYTES ? pos - GAP_BYTES : 0;

            if (candidateStart + trackLength <= captureLength)
            {
                trackStart = candidateStart;
                break;
            }
        }
    }

    // Extract the track only after its final start position is known.
    std::vector<uint8_t> bestTrack = extractBitAlignedTrack(rawTrack, captureLength, trackStart * 8, trackLength * 8);

    size_t bestHeaders = countValidHeaders(bestTrack);

    for (uint8_t phase = 1; phase < 8; ++phase)
    {
        const std::vector<uint8_t> candidate = extractBitAlignedTrack(rawTrack, captureLength, (trackStart * 8) + phase, trackLength * 8);
        const size_t validHeaders = countValidHeaders(candidate);

        if (validHeaders > bestHeaders)
        {
            bestTrack = candidate;
            bestHeaders = validHeaders;
        }
    }

    result.data = bestTrack;

    #ifdef Debug
        if ((density & 0x03) == 2)
        {
            std::cout
                << "[NIB:EXTRACT:HEADERS]"
                << " bytes=" << result.data.size()
                << " headers=" << countValidHeaders(result.data)
                << '\n';
        }
    #endif

    result.speedZones.assign(result.data.size(), density & 0x03);
    result.start  = trackStart;
    result.length = trackLength;
    result.valid  = true;

    return result;
}

std::vector<uint8_t> NIB::extractBitAlignedTrack(const uint8_t* data, size_t captureLength, size_t startBit, size_t bitLength) const
{
    std::vector<uint8_t> output;

    if (!data)
        return output;

    if (captureLength == 0 || bitLength == 0)
        return output;

    const size_t captureBits = captureLength * 8;
    const size_t outputBytes = (bitLength + 7) / 8;

    output.assign(outputBytes, 0);

    for (size_t bitIndex = 0; bitIndex < bitLength; ++bitIndex)
    {
        const size_t sourceBit = (startBit + bitIndex) % captureBits;
        const size_t sourceByte = sourceBit / 8;
        const int sourceBitInByte = 7 - static_cast<int>(sourceBit % 8);

        const bool bit = ((data[sourceByte] >> sourceBitInByte) & 0x01) != 0;

        if (bit)
        {
            const size_t outputByte = bitIndex / 8;
            const int outputBitInByte = 7 - static_cast<int>(bitIndex % 8);

            output[outputByte] |= static_cast<uint8_t>(1u << outputBitInByte);
        }
    }

    return output;
}

std::vector<uint8_t> NIB::buildBitPhaseView(const uint8_t* data, size_t captureLength, uint8_t phase) const
{
    std::vector<uint8_t> output;

    if (!data)
        return output;

    if (captureLength == 0)
        return output;

    phase &= 0x07;

    output.resize(captureLength);

    if (phase == 0)
    {
        std::copy(data, data + captureLength, output.begin());
        return output;
    }

    for (size_t i = 0; i < captureLength; ++i)
    {
        const size_t next = (i + 1) % captureLength;

        output[i] = static_cast<uint8_t>(
            (data[i] << phase) |
            (data[next] >> (8 - phase)));
    }

    return output;
}

size_t NIB::countValidHeaders(const std::vector<uint8_t>& trackData) const
{
    if (trackData.size() < 10)
        return 0;

    GCRCodec codec;

    size_t validHeaders = 0;

    for (size_t pos = 0; pos < trackData.size(); ++pos)
    {
        std::array<uint8_t, 10> encoded = {};

        for (size_t i = 0; i < encoded.size(); ++i)
        {
            encoded[i] =
                trackData[(pos + i) % trackData.size()];
        }

        std::vector<uint8_t> raw;
        raw.reserve(8);

        if (!codec.decodeBytes(encoded.data(), encoded.size(), raw))
            continue;

        if (raw.size() != 8)
            continue;

        if (raw[0] != 0x08)
            continue;

        const uint8_t sector = raw[2];
        const uint8_t track = raw[3];
        const uint8_t id2 = raw[4];
        const uint8_t id1 = raw[5];

        const uint8_t checksum =
            static_cast<uint8_t>(
                sector ^
                track ^
                id2 ^
                id1);

        if (raw[1] != checksum)
            continue;

        if (track < 1 || track > 35)
            continue;

        if (sector >= codec.sectorsPerTrack1541(track))
            continue;

        ++validHeaders;
    }

    return validHeaders;
}
