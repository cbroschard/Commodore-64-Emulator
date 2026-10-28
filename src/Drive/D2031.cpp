// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Drive/D2031.h"

D2031::D2031(int deviceNumber, const std::string& loRom, const std::string& hiRom)
{
    reset();

    setDeviceNumber(deviceNumber);

    d2031mem.attachPeripheralInstance(this);
    d2031Bus.attachMemoryInstance(&d2031mem);
    driveCPU.attachCPUBusInstance(&d2031Bus);
    driveCPU.attachIRQLineInstance(&IRQ);

    if (!d2031mem.initialize(loRom, hiRom))
        throw std::runtime_error("Unable to start drive, ROM not loaded!\n");
}

D2031::~D2031()
{

}

void D2031::reset()
{
    // Disk
    diskLoaded                  = false;
    diskWriteProtected          = false;
    currentTrack                = 17;
    currentSector               = 0;
    densityCode                 = 3;
    halfTrackPos                = currentTrack * 2;
    loadedDiskName.clear();

    gcrPos                      = 0;
    gcrBitCounter               = 0;
    gcrDirty                    = true;
    lastHeaderTrack             = 0;
    lastHeaderSector            = 0;
    haveLastHeader              = false;
    diskWriteGate               = false;
    pendingWritePos             = 0;
    pendingWritePosValid        = false;
    trackModifiedByWrite        = false;
    lastHeaderPos               = 0;
    lastHeaderValid             = false;
    readSyncRun                 = 0;
    readAfterSync               = false;
    lastRomHeaderTrack          = 0;
    lastRomHeaderSector         = 0;
    lastRomHeaderValid          = false;
    lastRomHeaderPos            = 0;
    lastRomHeaderCycle          = 0;
    writeSyncRun                = 0;
    writeAfterSync              = false;
    writeGapRun                 = 0;

    // UI activity
    uiTrack                     = currentTrack;
    uiSector                    = currentSector;
    uiLedWasOn                  = false;

    readGcrHeaderProbe.clear();
    writeGcrBuffer.clear();
    gcrTrack.clear();
    gcrSectorAtPos.clear();
    gcrWrittenMask.clear();

    invalidateRawGcrCache();
    updateIRQ();
}

void D2031::setDiskWriteGate(bool enabled)
{
    if (diskWriteGate == enabled)
        return;

    diskWriteGate = enabled;

    if (!enabled)
    {
        rebuildSyncMapForCurrentTrack();
        saveCurrentRawTrackToCache();

        writeGcrBuffer.clear();
        haveLastHeader = false;

        writeSyncRun = 0;
        writeAfterSync = false;
        writeGapRun = 0;

        pendingWritePos = 0;
        pendingWritePosValid = false;
    }
}

void D2031::onVIA2PortARead(uint8_t value)
{
    readGcrHeaderProbe.push_back(value);

    constexpr size_t HEADER_GCR_SIZE = 10;

    if (readGcrHeaderProbe.size() < HEADER_GCR_SIZE)
        return;

    while (readGcrHeaderProbe.size() > HEADER_GCR_SIZE)
        readGcrHeaderProbe.erase(readGcrHeaderProbe.begin());

    std::vector<uint8_t> raw;
    raw.reserve(8);

    if (!gcrCodec.decodeBytes(readGcrHeaderProbe.data(), HEADER_GCR_SIZE, raw))
        return;

    if (raw.size() != 8 || raw[0] != 0x08)
        return;

    const uint8_t sector = raw[2];
    const uint8_t track  = raw[3];
    const uint8_t id2    = raw[4];
    const uint8_t id1    = raw[5];

    const uint8_t expectedChecksum = static_cast<uint8_t>(sector ^ track ^ id2 ^ id1);

    if (raw[1] != expectedChecksum)
        return;

    if (track < 1 || track > 35)
        return;

    if (sector >= gcrCodec.sectorsPerTrack1541(track))
        return;

    lastRomHeaderTrack = track;
    lastRomHeaderSector = sector;
    lastRomHeaderValid = true;
    lastRomHeaderPos = gcrPos;
    lastRomHeaderCycle = 0;
}

void D2031::rebuildSyncMapForCurrentTrack()
{
    gcrTrack.rebuildSyncMap();
}

void D2031::saveCurrentRawTrackToCache()
{
    //
    // Generic raw-track media.
    //
    if (diskImage && diskImage->supportsRawTracks())
    {
        if (gcrTrack.empty())
            return;

        if (!trackModifiedByWrite)
            return;

        const size_t trackIndex = static_cast<size_t>(halfTrackPos);

        if (diskImage->writeRawTrack(trackIndex, gcrTrack))
            trackModifiedByWrite = false;

        return;
    }

    //
    // Synthesized track cache for sector media.
    //
    if (currentTrack >= rawGcrTrackCache.size())
        return;

    if (gcrTrack.empty())
        return;

    rawGcrTrackCache[currentTrack]  = gcrTrack.getTrackData();
    rawGcrSyncCache[currentTrack]   = gcrTrack.getSyncMap();
    rawGcrSectorCache[currentTrack] = gcrSectorAtPos;
    rawGcrTrackValid[currentTrack]  = true;

    if (trackModifiedByWrite)
        rawGcrTrackDirty[currentTrack] = true;
}

void D2031::invalidateRawGcrCache()
{
    for (auto& t : rawGcrTrackCache)
        t.clear();

    for (auto& s : rawGcrSyncCache)
        s.clear();

    for (auto& p : rawGcrSectorCache)
        p.clear();

    rawGcrTrackValid.fill(false);
    rawGcrTrackDirty.fill(false);

    gcrTrack.clear();
    gcrTrack.getSyncMap().clear();
    gcrSectorAtPos.clear();
    gcrWrittenMask.clear();

    gcrPos = 0;
    gcrDirty = true;
}
