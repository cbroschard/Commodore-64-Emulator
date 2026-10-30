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
    setDeviceNumber(deviceNumber);

    d2031mem.attachPeripheralInstance(this);
    d2031Bus.attachMemoryInstance(&d2031mem);
    driveCPU.attachCPUBusInstance(&d2031Bus);
    driveCPU.attachIRQLineInstance(&IRQ);

    if (!d2031mem.initialize(loRom, hiRom))
        throw std::runtime_error("Unable to start drive, ROM not loaded!\n");

    ieee488Device.setBusChangeCallback([this](const IEEE488Bus::BusState& oldState, const IEEE488Bus::BusState& newState)
    {
        onIEEE488BusChanged(oldState, newState);
    });

    reset();
}

D2031::~D2031()
{
    ieee488Device.detachBus();
}

void D2031::reset()
{
    motorOn                     = false;

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

    d2031mem.reset();
    driveCPU.reset();
}

void D2031::saveState(StateWriter& wrtr) const
{
    wrtr.beginChunk("D231");
    wrtr.writeU32(1); // Version
    wrtr.writeU8(static_cast<uint8_t>(deviceNumber));

    wrtr.writeBool(diskLoaded);
    wrtr.writeBool(diskWriteProtected);
    wrtr.writeString(loadedDiskName);

    wrtr.writeBool(motorOn);
    wrtr.writeU8(currentTrack);
    wrtr.writeU8(currentSector);
    wrtr.writeU8(densityCode);
    wrtr.writeI32(halfTrackPos);
    wrtr.writeI32(gcrBitCounter);
    wrtr.writeU32(static_cast<uint32_t>(gcrPos));

    // UI state
    wrtr.writeU8(uiTrack);
    wrtr.writeU8(uiSector);

    // CPU state
    driveCPU.saveStatePayload(wrtr);
    driveCPU.saveStateExtendedPayload(wrtr);

    d2031mem.saveState(wrtr);
    d2031mem.getVIA1().saveState(wrtr);
    d2031mem.getVIA2().saveState(wrtr);

    wrtr.endChunk();
}

bool D2031::loadState(const StateReader::Chunk& chunk, StateReader& rdr)
{
    if (std::memcmp(chunk.tag, "D231", 4) != 0)
        return false;

    rdr.enterChunkPayload(chunk);

    uint32_t ver = 0;
    if (!rdr.readU32(ver))                                  { rdr.exitChunkPayload(chunk); return false; }
    if (ver != 1)                                           { rdr.exitChunkPayload(chunk); return false; }

    uint8_t dev = 0;
    if (!rdr.readU8(dev))                                   { rdr.exitChunkPayload(chunk); return false; }

    setDeviceNumber(static_cast<int>(dev));

    bool savedDiskLoaded = false;
    bool savedWriteProtected = false;
    std::string savedDiskName;

    if (!rdr.readBool(savedDiskLoaded))                     { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readBool(savedWriteProtected))                 { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readString(savedDiskName))                     { rdr.exitChunkPayload(chunk); return false; }

    if (savedDiskLoaded)
    {
        if (savedDiskName.empty())                          { rdr.exitChunkPayload(chunk); return false; }

        loadDisk(savedDiskName);

        if (!diskLoaded || !diskImage)                      { rdr.exitChunkPayload(chunk); return false; }
    }
    else
    {
        resetForMediaChange();

        diskImage.reset();
        loadedDiskName.clear();
        diskLoaded = false;
        diskWriteProtected = false;
    }

    diskLoaded = savedDiskLoaded;
    diskWriteProtected = savedWriteProtected;
    loadedDiskName = savedDiskLoaded ? savedDiskName : std::string{};

    if (!rdr.readBool(motorOn))                             { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readU8(currentTrack))                          { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readU8(currentSector))                         { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readU8(densityCode))                           { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readI32(halfTrackPos))                         { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readI32(gcrBitCounter))                        { rdr.exitChunkPayload(chunk); return false; }

    uint32_t savedGcrPos = 0;

    if (!rdr.readU32(savedGcrPos))                          { rdr.exitChunkPayload(chunk); return false; }

    if (!rdr.readU8(uiTrack))                               { rdr.exitChunkPayload(chunk); return false; }
    if (!rdr.readU8(uiSector))                              { rdr.exitChunkPayload(chunk); return false; }

    if (!driveCPU.loadStatePayload(rdr))                    { rdr.exitChunkPayload(chunk); return false; }
    if (!driveCPU.loadStateExtendedPayload(chunk, rdr))     { rdr.exitChunkPayload(chunk); return false; }

    if (!d2031mem.loadState(rdr))                           { rdr.exitChunkPayload(chunk); return false; }
    if (!d2031mem.getVIA1().loadState(rdr))                 { rdr.exitChunkPayload(chunk); return false; }
    if (!d2031mem.getVIA2().loadState(rdr))                 { rdr.exitChunkPayload(chunk); return false; }

    rdr.exitChunkPayload(chunk);

    d2031mem.getVIA1().synchronizeIEEE488Outputs();

    // The raw GCR vectors and caches are not serialized.
    // Rebuild them lazily while retaining the rotational position.
    invalidateRawGcrCache();

    gcrPos = static_cast<size_t>(savedGcrPos);
    gcrDirty = true;

    updateIRQ();

    uiLedWasOn = d2031mem.getVIA2().isLedOn();

    return true;
}

bool D2031::attachIEEE488Bus(IEEE488Bus* bus)
{
    if (!bus)
        return false;

    const auto id = static_cast<IEEE488Bus::DeviceId>(getDeviceNumber());

    if (!ieee488Device.attachBus(bus, id))
        return false;

    if (!bus->registerDrive(id, this))
    {
        ieee488Device.detachBus();
        return false;
    }

    d2031mem.getVIA1().synchronizeIEEE488Outputs();

    return true;
}

void D2031::onIEEE488BusChanged(const IEEE488Bus::BusState& oldState, const IEEE488Bus::BusState& newState)
{
    d2031mem.getVIA1().setIEEE488Inputs(oldState, newState);
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

void D2031::onVIA2PortAWrite(uint8_t value, uint8_t ddrA)
{
    if (!diskWriteGate)
        return;

    if (!diskLoaded || !diskImage || !motorOn || diskWriteProtected)
        return;

    if (ddrA != 0xFF)
        return;

    if (!pendingWritePosValid || gcrTrack.empty())
        return;

    const size_t pos = pendingWritePos % gcrTrack.size();

    gcrTrack.getTrackData()[pos] = value;

    if (gcrWrittenMask.size() == gcrTrack.size())
        gcrWrittenMask[pos] = 1;

    trackModifiedByWrite = true;

    //
    // Sector-backed images keep their synthesized raw-track cache
    // synchronized during writes.
    //
    // Native raw-track formats are committed through writeRawTrack()
    // when the write gate closes.
    //
    if (diskImage && !diskImage->supportsRawTracks())
        saveCurrentRawTrackToCache();

    acceptGCRWriteByte(value);
}

void D2031::onStepperPhaseChange(uint8_t oldPhase, uint8_t newPhase)
{
    const int oldIdx = stepIndex(oldPhase);
    const int newIdx = stepIndex(newPhase);

    if (oldIdx < 0 || newIdx < 0 || oldIdx == newIdx)
        return;

    // delta in [0..7]
    const int delta = (newIdx - oldIdx + 8) & 7;

    int step = 0;
    if (delta == 2)      step = +1;   // forward one half-track
    else if (delta == 6) step = -1;   // backward one half-track
    else
        return; // ignore illegal jumps (delta 2..6)

    saveCurrentRawTrackToCache();

    int maxHalfTrack = 34 * 2;

    if (diskImage && diskImage->supportsRawTracks())
    {
        const size_t trackCount = diskImage->getHalfTrackCount();

        if (trackCount > 0)
            maxHalfTrack = static_cast<int>(trackCount) - 1;
    }

    halfTrackPos = std::clamp(halfTrackPos + step, 0, maxHalfTrack);
    currentTrack = static_cast<uint8_t>(halfTrackPos / 2);

    uiTrack = currentTrack;
    uiSector = currentSector;

    gcrDirty = true;
}

void D2031::setDensityCode(uint8_t code)
{
    code &= 0x03;
    if (densityCode != code) densityCode = code;
}

void D2031::acceptGCRWriteByte(uint8_t value)
{
    writeGcrBuffer.push_back(value);

    // Keep bounded so noise does not grow forever.
    if (writeGcrBuffer.size() > 4096)
        writeGcrBuffer.erase(writeGcrBuffer.begin(), writeGcrBuffer.begin() + 1024);
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

void D2031::resetForMediaChange()
{
    // --- Media/GCR reset ---
    gcrPos = 0;
    gcrBitCounter = 0;
    gcrDirty = true;
    lastHeaderTrack = 0;
    lastHeaderSector = 0;
    haveLastHeader = false;
    diskWriteGate = false;
    pendingWritePos = 0;
    pendingWritePosValid = false;
    trackModifiedByWrite = false;
    lastHeaderPos = 0;
    lastHeaderValid = false;
    readSyncRun = 0;
    readAfterSync = false;
    lastRomHeaderTrack = 0;
    lastRomHeaderSector = 0;
    lastRomHeaderValid = false;
    lastRomHeaderPos = 0;
    lastRomHeaderCycle = 0;
    writeSyncRun = 0;
    writeAfterSync = false;
    writeGapRun = 0;
    readGcrHeaderProbe.clear();
    gcrTrack.clear();
    gcrTrack.getSyncMap().clear();
    gcrSectorAtPos.clear();
    gcrWrittenMask.clear();
    writeGcrBuffer.clear();
    invalidateRawGcrCache();

    updateIRQ();
}
