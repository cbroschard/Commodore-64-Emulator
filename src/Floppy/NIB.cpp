// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
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

    dirty = false;

    return true;
}

bool NIB::saveDisk(const std::string& filePath)
{
    return true;
}

bool NIB::readRawTrack(size_t halfTrack, GCRTrackStream& outTrack) const
{
    (void)halfTrack;
    (void)outTrack;
    return false;
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
    return true;
}
