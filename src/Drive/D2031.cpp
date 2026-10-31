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

    d2031mem.reset();
    driveCPU.reset();

    updateIRQ();
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

void D2031::tick(uint32_t cycles)
{
    int32_t remaining = static_cast<int32_t>(cycles);

    while (remaining > 0)
    {
        if (checkBreakpoint())
            return;

        // Execute the D2031 CPU.
        driveCPU.tick();

        uint32_t dc = driveCPU.getElapsedCycles();
        if (dc == 0)
            dc = 1;

        // Advance both 6522 VIA chips.
        d2031mem.tick(dc);

        // Advance disk rotation and GCR processing.
        if (motorOn && diskLoaded)
            gcrAdvance(dc);

        // Track activity LED state for the UI.
        const bool ledOn = d2031mem.getVIA2().isLedOn();

        if (ledOn)
            uiTrack = currentTrack;

        if (ledOn && !uiLedWasOn)
            uiSector = currentSector;

        uiLedWasOn = ledOn;

        remaining -= static_cast<int32_t>(dc);
    }
}

void D2031::updateIRQ()
{
    const bool via1IRQ = d2031mem.getVIA1().checkIRQActive();
    const bool via2IRQ = d2031mem.getVIA2().checkIRQActive();

    const bool any = via1IRQ || via2IRQ;

    if (any)
        IRQ.raiseIRQ(IRQLine::D2031_IRQ);
    else
        IRQ.clearIRQ(IRQLine::D2031_IRQ);
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

    // Consume this byte-ready slot.
    pendingWritePosValid = false;

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

    if (oldIdx == newIdx)
        return;

    const int delta = (newIdx - oldIdx + 8) & 7;

    int step = 0;

    if (delta == 2)
        step = +1;
    else if (delta == 6)
        step = -1;
    else
        return;

    int maxHalfTrack = 34 * 2;

    if (diskImage && diskImage->supportsRawTracks())
    {
        const size_t trackCount = diskImage->getHalfTrackCount();

        if (trackCount > 0)
            maxHalfTrack = static_cast<int>(trackCount) - 1;
    }

    const int nextHalfTrack = std::clamp(halfTrackPos + step, 0, maxHalfTrack);

    // No movement at the mechanical stop.
    if (nextHalfTrack == halfTrackPos)
        return;

    // Save the old track before changing position.
    saveCurrentRawTrackToCache();

    // Do not carry the old live-track write state
    // into the newly selected track.
    trackModifiedByWrite = false;
    pendingWritePos = 0;
    pendingWritePosValid = false;

    halfTrackPos = nextHalfTrack;
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

void D2031::getDriveIndicators(std::vector<Indicator>& out) const
{
    out.clear();

    Indicator act;
    act.name = "ACT";
    act.on = d2031mem.getVIA2().isLedOn();
    act.color = IDriveIndicatorView::DriveIndicatorColor::Red;

    out.push_back(std::move(act));
}

void D2031::loadDisk(const std::string& path)
{
    // Preserve pending writes from the currently mounted image
    // before replacing it.
    if (diskImage)
        flushAndSaveDisk();

    resetForMediaChange();

    auto img = DiskFactory::create(path);

    if (!img)
    {
        // Door open / no media behavior.
        // Do not reset the drive CPU or IEEE-488 interface.
        loadedDiskName.clear();
        diskImage.reset();
        diskLoaded = false;
        diskWriteProtected = false;
        lastError = DriveError::NO_DISK;
        status = DriveStatus::IDLE;

        // Invalidate the previous media stream.
        gcrDirty = true;
        gcrPos = 0;
        gcrBitCounter = 0;

        gcrTrack.clear();
        gcrTrack.getSyncMap().clear();
        gcrSectorAtPos.clear();

        d2031mem.getVIA2().clearMechBytePending();

        return;
    }

    if (!img->loadDisk(path))
    {
        // Invalid or unreadable image.
        // Preserve the drive computer, but remove media.
        loadedDiskName.clear();
        diskImage.reset();
        diskLoaded = false;
        diskWriteProtected = false;
        lastError = DriveError::NO_DISK;
        status = DriveStatus::IDLE;

        gcrDirty = true;
        gcrPos = 0;
        gcrBitCounter = 0;

        gcrTrack.clear();
        gcrTrack.getSyncMap().clear();
        gcrSectorAtPos.clear();

        d2031mem.getVIA2().clearMechBytePending();

        return;
    }

    // HOT SWAP
    diskImage = std::move(img);

    diskWriteProtected = diskImage->isWriteProtected();
    diskLoaded = true;

    invalidateRawGcrCache();

    loadedDiskName = path;
    status = DriveStatus::READY;
    lastError = DriveError::NONE;

    // Rebuild the media stream for the newly inserted disk.
    gcrDirty = true;
    gcrPos = 0;
    gcrBitCounter = 0;

    gcrTrack.clear();
    gcrTrack.getSyncMap().clear();
    gcrSectorAtPos.clear();

    d2031mem.getVIA2().clearMechBytePending();
}

void D2031::unloadDisk()
{
    // Commit any pending disk writes before ejecting.
    flushAndSaveDisk();

    // Remove the disk image.
    diskImage.reset();
    loadedDiskName.clear();

    // Reset media stream state.
    gcrPos = 0;
    gcrBitCounter = 0;

    gcrTrack.clear();
    gcrTrack.getSyncMap().clear();
    gcrSectorAtPos.clear();

    writeGcrBuffer.clear();
    gcrWrittenMask.clear();

    // Reset write state.
    diskWriteGate        = false;
    pendingWritePos      = 0;
    pendingWritePosValid = false;
    trackModifiedByWrite = false;

    invalidateRawGcrCache();

    // Reset disk state.
    diskLoaded         = false;
    diskWriteProtected = false;

    // Return the mechanism to your existing default position.
    currentTrack = 17;
    currentSector = 0;
    halfTrackPos = currentTrack * 2;

    // UI state.
    uiTrack = currentTrack;
    uiSector = currentSector;
    uiLedWasOn = false;

    // Drive status.
    lastError = DriveError::NONE;
    status = DriveStatus::IDLE;

    // Clear header information.
    lastHeaderTrack = 0;
    lastHeaderSector = 0;
    haveLastHeader = false;

    // Clear any pending mechanical read data.
    d2031mem.getVIA2().clearMechBytePending();
}

void D2031::startMotor()
{
    motorOn = true;
}

void D2031::stopMotor()
{
    // Preserve the current raw track before stopping.
    if (diskLoaded && diskImage)
        saveCurrentRawTrackToCache();

    motorOn = false;
}

void D2031::flushAndSaveDisk()
{
    if (!diskImage || loadedDiskName.empty())
        return;

    if (diskImage->supportsRawTracks())
    {
        //
        // Push the live raw GCR track into the disk image.
        //
        saveCurrentRawTrackToCache();
    }
    else if (diskImage->supportsSectorAccess())
    {
        //
        // Decode modified GCR tracks back into disk sectors.
        //
        flushAllDirtyRawTracksToImage();
    }

    // Persist modified disk data to the image file.
    if (diskImage->isDirty())
    {
        if (diskImage->saveDisk(loadedDiskName))
            diskImage->clearDirty();
    }
}

bool D2031::gcrTick()
{
    // Rebuild the current GCR track when necessary.
    if (gcrDirty)
    {
        const size_t oldPos = gcrPos;

        loadCurrentRawTrackFromCacheOrBuild();

        gcrDirty = false;

        if (!gcrTrack.empty())
            gcrPos = oldPos % gcrTrack.size();
        else
            gcrPos = 0;
    }

    if (gcrTrack.empty())
        return false;

    // Ensure the synchronization map matches the track.
    if (gcrTrack.getSyncMap().size() != gcrTrack.size())
        gcrTrack.getSyncMap().assign(gcrTrack.size(), 0);

    // Ensure sector metadata matches the current track.
    if (gcrSectorAtPos.size() != gcrTrack.size())
        gcrSectorAtPos.assign(gcrTrack.size(), currentSector);

    const size_t pos = gcrPos;

    const uint8_t gcrByte = gcrTrack.getTrackData()[pos];
    const bool syncHigh = (gcrTrack.getSyncMap()[pos] != 0);
    const uint8_t sectorNow = gcrSectorAtPos[pos];

    // Sector-backed images use generated sector tags.
    // Raw G64/NIB images determine sectors from decoded headers.
    if (diskImage && diskImage->supportsSectorAccess())
        currentSector = sectorNow;

    // Sample the disk header when not writing.
    if (!diskWriteGate)
        sampleHeaderAtCurrentPosition(pos);

    // Advance rotational position.
    gcrPos = (gcrPos + 1) % gcrTrack.size();

    // Writing: notify VIA2 that the next byte can be written.
    if (diskWriteGate &&
        motorOn &&
        diskLoaded &&
        diskImage &&
        !diskWriteProtected)
    {
        pendingWritePos = pos;
        pendingWritePosValid = true;

        d2031mem.getVIA2().pulseWriteByteReady();

        return true;
    }

    // Reading: deliver the current GCR byte and sync state
    // to the D2031's mechanical VIA.
    d2031mem.getVIA2().diskByteFromMedia(gcrByte, syncHigh);

    return true;
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

void D2031::loadCurrentRawTrackFromCacheOrBuild()
{
    pendingWritePos = 0;
    pendingWritePosValid = false;

    //
    // Generic raw-track media path (G64, NIB, etc.).
    //
    if (diskImage && diskImage->supportsRawTracks())
    {
        gcrTrack.clear();
        gcrSectorAtPos.clear();
        gcrWrittenMask.clear();

        const size_t trackIndex = static_cast<size_t>(halfTrackPos);

        if (!diskImage->hasRawTrack(trackIndex))
        {
            gcrPos = 0;
            d2031mem.getVIA2().clearMechBytePending();
            return;
        }

        if (!diskImage->readRawTrack(trackIndex, gcrTrack))
        {
            gcrPos = 0;
            d2031mem.getVIA2().clearMechBytePending();
            return;
        }

        if (gcrTrack.empty())
        {
            gcrPos = 0;
            d2031mem.getVIA2().clearMechBytePending();
            return;
        }

        //
        // Raw preservation formats do not provide a logical
        // sector-position map.
        //
        gcrSectorAtPos.assign(
            gcrTrack.size(),
            currentSector);

        gcrWrittenMask.assign(
            gcrTrack.size(),
            0);

        gcrPos %= gcrTrack.size();

        d2031mem.getVIA2().clearMechBytePending();

        return;
    }

    //
    // Sector-backed media path (D64, etc.).
    //
    if (currentTrack >= rawGcrTrackCache.size())
        return;

    if (rawGcrTrackValid[currentTrack])
    {
        gcrTrack.getTrackData() =
            rawGcrTrackCache[currentTrack];

        gcrTrack.getSyncMap() =
            rawGcrSyncCache[currentTrack];

        gcrSectorAtPos =
            rawGcrSectorCache[currentTrack];

        if (gcrWrittenMask.size() != gcrTrack.size())
        {
            gcrWrittenMask.assign(
                gcrTrack.size(),
                0);
        }

        if (!gcrTrack.empty())
            gcrPos %= gcrTrack.size();
        else
            gcrPos = 0;

        d2031mem.getVIA2().clearMechBytePending();

        return;
    }

    //
    // Build a GCR track from the sector image.
    //
    rebuildGCRTrackStream();

    rawGcrTrackCache[currentTrack] =
        gcrTrack.getTrackData();

    rawGcrSyncCache[currentTrack] =
        gcrTrack.getSyncMap();

    rawGcrSectorCache[currentTrack] =
        gcrSectorAtPos;

    rawGcrTrackValid[currentTrack] = true;
    rawGcrTrackDirty[currentTrack] = false;
}

void D2031::saveCurrentRawTrackToCache()
{
    if (!diskImage || gcrTrack.empty())
        return;

    // Native raw-track media (G64/NIB).
    if (diskImage->supportsRawTracks())
    {
        if (!trackModifiedByWrite)
            return;

        const size_t trackIndex = static_cast<size_t>(halfTrackPos);

        if (diskImage->writeRawTrack(trackIndex, gcrTrack))
            trackModifiedByWrite = false;

        return;
    }

    // Sector-backed media (D64).
    if (currentTrack >= rawGcrTrackCache.size())
        return;

    rawGcrTrackCache[currentTrack] = gcrTrack.getTrackData();
    rawGcrSyncCache[currentTrack] = gcrTrack.getSyncMap();
    rawGcrSectorCache[currentTrack] = gcrSectorAtPos;

    rawGcrTrackValid[currentTrack] = true;

    if (trackModifiedByWrite)
    {
        rawGcrTrackDirty[currentTrack] = true;
        trackModifiedByWrite = false;
    }
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

void D2031::gcrAdvance(uint32_t dc)
{
    gcrBitCounter += static_cast<int>(dc);

    while (true)
    {
        uint8_t activeDensity = densityCode;

        const auto& speedZones = gcrTrack.getSpeedZones();

        if (!speedZones.empty())
        {
            const size_t speedPos = gcrPos % speedZones.size();
            activeDensity = speedZones[speedPos] & 0x03;
        }

        const int cyclesPerByte = cyclesPerByteFromDensity(activeDensity);

        if (gcrBitCounter < cyclesPerByte)
            break;

        gcrBitCounter -= cyclesPerByte;

        gcrTick();
    }
}

int D2031::cyclesPerByteFromDensity(uint8_t code) const
{
    static constexpr int kCycles[4] = { 32, 30, 28, 26 };

    return kCycles[code & 0x03];
}

void D2031::sampleHeaderAtCurrentPosition(size_t pos)
{
    if (gcrTrack.empty())
        return;

    constexpr size_t HEADER_GCR_SIZE = 10;

    const size_t n = gcrTrack.size();

    if (pos >= n)
        return;

    const size_t prev = (pos + n - 1) % n;

    if (prev >= gcrTrack.getSyncMap().size() ||
        gcrTrack.getSyncMap()[prev] == 0)
        return;

    uint8_t gcrHeader[HEADER_GCR_SIZE];

    for (size_t i = 0; i < HEADER_GCR_SIZE; ++i)
        gcrHeader[i] = gcrTrack.getTrackData()[(pos + i) % n];

    std::vector<uint8_t> raw;
    raw.reserve(8);

    if (!gcrCodec.decodeBytes(gcrHeader, HEADER_GCR_SIZE, raw))
        return;

    if (raw.size() != 8 || raw[0] != 0x08)
        return;

    const uint8_t sector = raw[2];
    const uint8_t track  = raw[3];
    const uint8_t id2    = raw[4];
    const uint8_t id1    = raw[5];

    const uint8_t expectedChecksum =  static_cast<uint8_t>(sector ^ track ^ id2 ^ id1);

    if (raw[1] != expectedChecksum)
        return;

    if (track < 1 || track > 35)
        return;

    if (sector >= gcrCodec.sectorsPerTrack1541(track))
        return;

    lastHeaderTrack = track;
    lastHeaderSector = sector;
    lastHeaderPos = pos;
    lastHeaderValid = true;
    haveLastHeader = true;

    // Native G64/NIB tracks determine their logical sector
    // from the decoded on-disk sector headers.
    if (diskImage && diskImage->supportsRawTracks())
        currentSector = sector;
}

void D2031::rebuildGCRTrackStream()
{
    gcrTrack.clear();
    gcrTrack.getSyncMap().clear();
    gcrSectorAtPos.clear();

    if (!diskLoaded || !diskImage)
        return;

    CBMImage* cbmImage = getCBMImage();

    // Build GCR data only for sector-addressable media.
    // G64/NIB images use the raw-track path.
    if (!cbmImage)
        return;

    // Preserve the D1541's zero-based internal track convention.
    const int track1based = static_cast<int>(currentTrack) + 1;

    if (track1based < 1 || track1based > 35)
        return;

    const int spt = gcrCodec.sectorsPerTrack1541(track1based);

    // Read the BAM to obtain the disk ID.
    auto bam = cbmImage->readSector(18, 0);

    if (bam.size() < 256)
        bam.resize(256, 0x00);

    const uint8_t id1 = bam[0xA2];
    const uint8_t id2 = bam[0xA3];

    // Append repeated bytes, sync markers, and sector tags.
    auto pushN = [&](uint8_t value, int count, bool isSync, uint8_t sectorTag)
    {
        auto& trackData = gcrTrack.getTrackData();

        trackData.insert(trackData.end(), count, value);

        gcrTrack.getSyncMap().insert(gcrTrack.getSyncMap().end(), count, isSync ? 1 : 0);
        gcrSectorAtPos.insert(gcrSectorAtPos.end(), count, sectorTag);
    };

    // Encode groups of four bytes into five GCR bytes.
    auto pushEncoded = [&](const uint8_t* input, size_t length, uint8_t sectorTag)
    {
        for (size_t i = 0; i < length; i += 4)
        {
            uint8_t encoded[5];

            gcrCodec.encode4Bytes(&input[i], encoded);

            auto& trackData = gcrTrack.getTrackData();

            trackData.insert(trackData.end(), encoded, encoded + 5);

            gcrTrack.getSyncMap().insert(gcrTrack.getSyncMap().end(), 5, 0);
            gcrSectorAtPos.insert(gcrSectorAtPos.end(), 5, sectorTag);
        }
    };

    constexpr int SYNC_LEN   = 10;
    constexpr int HEADER_GAP = 9;
    constexpr int TAIL_GAP   = 9;

    // Lead-in gap (not a sync region).
    pushN(0x55, 64, false, 0);

    for (int sector = 0; sector < spt; ++sector)
    {
        std::vector<uint8_t> sec = cbmImage->readSector(static_cast<uint8_t>(track1based), static_cast<uint8_t>(sector));

        if (sec.size() != 256)
            sec.assign(256, 0x00);

        // -------------------------------------------------
        // Sector header
        // -------------------------------------------------
        pushN(0xFF, SYNC_LEN, true, static_cast<uint8_t>(sector));

        uint8_t hdr[8] = {};

        hdr[0] = 0x08;
        hdr[2] = static_cast<uint8_t>(sector);
        hdr[3] = static_cast<uint8_t>(track1based);
        hdr[4] = id2;
        hdr[5] = id1;
        hdr[6] = 0x0F;
        hdr[7] = 0x0F;

        hdr[1] = static_cast<uint8_t>(hdr[2] ^ hdr[3] ^ hdr[4] ^ hdr[5]);

        pushEncoded(hdr, 8, static_cast<uint8_t>(sector));
        pushN(0x55, HEADER_GAP, false, static_cast<uint8_t>(sector));

        // -------------------------------------------------
        // Sector data
        // -------------------------------------------------
        pushN(0xFF, SYNC_LEN, true, static_cast<uint8_t>(sector));

        std::vector<uint8_t> raw(260, 0x00);

        raw[0] = 0x07;

        uint8_t checksum = 0;

        for (int i = 0; i < 256; ++i)
        {
            raw[1 + i] = sec[i];
            checksum ^= sec[i];
        }

        raw[257] = checksum;
        raw[258] = 0x00;
        raw[259] = 0x00;

        pushEncoded(raw.data(), raw.size(), static_cast<uint8_t>(sector));
        pushN(0x55, TAIL_GAP, false, static_cast<uint8_t>(sector));
    }

    // Trailing gap.
    pushN(0x55, 128, false, 0);

    // Ensure synchronization metadata matches track length.
    if (gcrTrack.getSyncMap().size() != gcrTrack.size())
    {
        gcrTrack.getSyncMap().assign(
            gcrTrack.size(), 0);
    }

    gcrPos = 0;

    gcrWrittenMask.assign(gcrTrack.size(), 0);
    d2031mem.getVIA2().clearMechBytePending();
}

void D2031::flushAllDirtyRawTracksToImage()
{
    if (!diskLoaded || !diskImage)
        return;

    if (!diskImage->supportsSectorAccess() || diskImage->supportsRawTracks())
        return;

    // Capture pending changes on the active track.
    saveCurrentRawTrackToCache();

    // Preserve the complete mechanical and GCR state.
    const uint8_t savedTrack = currentTrack;
    const size_t savedPos = gcrPos;
    const bool savedGcrDirty = gcrDirty;

    const GCRTrackStream savedGcrTrack = gcrTrack;
    const std::vector<uint8_t> savedSectorMap =
        gcrSectorAtPos;

    // Decode all dirty cached tracks.
    for (size_t t = 0; t < rawGcrTrackDirty.size(); ++t)
    {
        if (!rawGcrTrackDirty[t])
            continue;

        if (!rawGcrTrackValid[t])
            continue;

        currentTrack = static_cast<uint8_t>(t);

        gcrTrack.getTrackData() = rawGcrTrackCache[t];
        gcrTrack.getSyncMap() = rawGcrSyncCache[t];
        gcrSectorAtPos = rawGcrSectorCache[t];

        // Position is irrelevant while decoding sectors.
        gcrPos = 0;

        flushCurrentRawTrackToImage();
    }

    // Restore the active drive state.
    currentTrack = savedTrack;
    gcrTrack = savedGcrTrack;
    gcrSectorAtPos = savedSectorMap;
    gcrPos = savedPos;
    gcrDirty = savedGcrDirty;
}

void D2031::flushCurrentRawTrackToImage()
{
    if (!diskImage->supportsSectorAccess() || diskImage->supportsRawTracks())
        return;

    if (!diskLoaded || !diskImage)
        return;

    CBMImage* cbmImage = getCBMImage();

    if (!cbmImage)
        return;

    if (currentTrack >= rawGcrTrackDirty.size())
        return;

    if (!rawGcrTrackDirty[currentTrack])
        return;

    // Internal track numbers are zero-based.
    // D64 sector addressing is one-based.
    const uint8_t track1based = static_cast<uint8_t>(currentTrack + 1);

    const int spt = gcrCodec.sectorsPerTrack1541(track1based);

    int written = 0;
    int failed = 0;

    for (int sector = 0; sector < spt; ++sector)
    {
        std::vector<uint8_t> sectorBytes;

        // Decode the sector from the current GCR track.
        if (!decodeRawSectorFromCurrentTrack(track1based, static_cast<uint8_t>(sector), sectorBytes))
        {
            ++failed;
            continue;
        }

        // Each Commodore sector must contain 256 bytes.
        if (sectorBytes.size() != 256)
        {
            ++failed;
            continue;
        }

        // Write decoded sector data to the image.
        if (cbmImage->writeSector(track1based, static_cast<uint8_t>(sector), sectorBytes))
            ++written;
        else
            ++failed;
    }

    // Only mark the track clean if every sector
    // was successfully decoded and written.
    if (failed == 0 && written == spt)
        rawGcrTrackDirty[currentTrack] = false;
}

bool D2031::decodeRawSectorFromCurrentTrack(uint8_t track, uint8_t sector, std::vector<uint8_t>& outSector)
{
    outSector.clear();

    if (gcrTrack.empty())
        return false;

    const size_t headerPos = findHeaderPosForSector(track, sector);

    if (headerPos == SIZE_MAX)
        return false;

    const size_t n = gcrTrack.size();

    constexpr size_t DATA_GCR_SIZE = 325;

    // After the sector header, search for the corresponding
    // GCR data block.
    //
    // Do not assume a fixed offset because modified tracks
    // may contain different gap lengths.
    const size_t scanStart = (headerPos + 10) % n;

    for (size_t offset = 0; offset < 128; ++offset)
    {
        const size_t dataStart = (scanStart + offset) % n;

        std::vector<uint8_t> gcrBlock;
        gcrBlock.reserve(DATA_GCR_SIZE);

        // Extract the complete encoded data block.
        // Handle wrapping around the circular track.
        for (size_t i = 0; i < DATA_GCR_SIZE; ++i)
            gcrBlock.push_back(gcrTrack.getTrackData()[(dataStart + i) % n]);

        std::vector<uint8_t> raw;
        raw.reserve(260);

        // Decode 325 GCR bytes into 260 raw bytes.
        if (!gcrCodec.decodeBytes(gcrBlock.data(), DATA_GCR_SIZE, raw))
            continue;

        if (raw.size() != 260)
            continue;

        // Commodore sector data block marker.
        if (raw[0] != 0x07)
            continue;

        // Validate sector data checksum.
        uint8_t checksum = 0;

        for (int i = 0; i < 256; ++i)
            checksum ^= raw[1 + i];

        if (checksum != raw[257])
            continue;

        // Extract the 256-byte sector payload.
        outSector.assign(raw.begin() + 1, raw.begin() + 257);

        return true;
    }

    return false;
}

size_t D2031::findHeaderPosForSector(uint8_t track, uint8_t sector) const
{
    if (gcrTrack.empty())
        return SIZE_MAX;

    constexpr size_t HEADER_GCR_SIZE = 10;

    const size_t n = gcrTrack.size();

    for (size_t pos = 0; pos < n; ++pos)
    {
        const size_t prev = (pos + n - 1) % n;

        // Only examine positions immediately following
        // a GCR synchronization region.
        if (prev >= gcrTrack.getSyncMap().size() || gcrTrack.getSyncMap()[prev] == 0)
            continue;

        uint8_t gcrHeader[HEADER_GCR_SIZE];

        // Read the encoded sector header, allowing the
        // header to wrap around the circular track.
        for (size_t i = 0; i < HEADER_GCR_SIZE; ++i)
            gcrHeader[i] = gcrTrack.getTrackData()[(pos + i) % n];

        std::vector<uint8_t> raw;
        raw.reserve(8);

        // Decode 10 GCR bytes into 8 raw bytes.
        if (!gcrCodec.decodeBytes(gcrHeader, HEADER_GCR_SIZE, raw))
            continue;

        if (raw.size() != 8 || raw[0] != 0x08)
            continue;

        const uint8_t decodedSector = raw[2];
        const uint8_t decodedTrack  = raw[3];
        const uint8_t id2           = raw[4];
        const uint8_t id1           = raw[5];

        // Validate the Commodore sector-header checksum.
        const uint8_t expectedChecksum = static_cast<uint8_t>(decodedSector ^ decodedTrack ^ id2 ^ id1);

        if (raw[1] != expectedChecksum)
            continue;

        // Return the GCR position of the matching header.
        if (decodedTrack == track && decodedSector == sector)
            return pos;
    }

    return SIZE_MAX;
}

CBMImage* D2031::getCBMImage()
{
    return dynamic_cast<CBMImage*>(diskImage.get());
}

const CBMImage* D2031::getCBMImage() const
{
    return dynamic_cast<const CBMImage*>(diskImage.get());
}
