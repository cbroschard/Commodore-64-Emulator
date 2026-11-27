// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#include "Drive/D4040Mechanism.h"

D4040Mechanism::D4040Mechanism() :
    diskInserted(false),
    writeProtected(false),
    motorCommandOn(false),
    motorOn(false),
    motorSpinDownCycles(0),
    track(18),
    sector(0),
    stepperPhase(0),
    halfTrack(36),
    stepperInitialized(false),
    syncDetected(false),
    byteReady(false),
    readLatch(0xFF),
    writeLatch(0xFF),
    gcrPos(0),
    loadedGCRTrack(0),
    gcrValid(false),
    gcrCycleAccumulator(0),
    gcrDirty(false),
    gcrBytesWritten(0),
    gcrSyncBytesWritten(0),
    gcrEncodedCount(0),
    gcrDecodedIndex(4)
{
    gcrEncodedBuffer[5] = {};
    gcrDecodedBuffer[4] = {};
}

D4040Mechanism::~D4040Mechanism() = default;

void D4040Mechanism::reset()
{
        motorCommandOn = false;
        motorOn = false;
        motorSpinDownCycles = 0;

        stepperPhase = 0;

        syncDetected = false;
        byteReady = false;

        readLatch = 0xFF;
        writeLatch = 0xFF;

        // Preserve physical head position across a controller reset.
        // A reset does not physically reposition the disk head.
        track = static_cast<uint8_t>(halfTrack / 2);

        // Force the next electrical stepper phase to establish
        // a new baseline without moving the head.
        stepperInitialized = false;

        // Preserve unsaved GCR data before resetting
        // the active mechanism state.
        cacheActiveTrack();
        gcrTrack.clear();

        gcrPos = 0;
        loadedGCRTrack = 0;
        gcrValid = false;
        gcrCycleAccumulator = 0;
        gcrDirty = false;

        resetGCRWriteStatistics();
        resetGCRDecoder();
}

void D4040Mechanism::updateStepperPhase(uint8_t newPhase)
{
        newPhase &= 0x03;

        // First observation establishes the phase baseline.
        // It must not move the head.
        if (!stepperInitialized)
        {
            stepperPhase = newPhase;
            stepperInitialized = true;
            return;
        }

        if (newPhase == stepperPhase)
            return;

        const uint8_t oldPhase = stepperPhase;

        // Four-phase stepper sequence.
        const uint8_t forward = static_cast<uint8_t>((oldPhase + 1) & 0x03);
        const uint8_t backward = static_cast<uint8_t>((oldPhase + 3) & 0x03);

        if (newPhase == forward)
        {
            // Move inward.
            if (halfTrack < 70)
                ++halfTrack;
        }
        else if (newPhase == backward)
        {
            // Move outward.
            if (halfTrack > 2)
                --halfTrack;
        }

        // Ignore non-adjacent phase jumps for movement,
        // but remember the latest electrical phase.
        stepperPhase = newPhase;

        // Update displayed track.
        track = static_cast<uint8_t>(halfTrack / 2);
}

bool D4040Mechanism::writeGCRByte(size_t position, uint8_t value, bool writeSync)
{
        if (!diskImage || writeProtected || !gcrValid)
            return false;

        auto& data = gcrTrack.getTrackData();
        auto& sync = gcrTrack.getSyncMap();

        if (data.empty() || sync.size() != data.size())
            return false;

        position %= data.size();

        if (writeSync)
        {
            // Approximate a continuous sync field
            // using bytes containing all one bits.
            data[position] = 0xFF;
            sync[position] = 1;
            ++gcrSyncBytesWritten;
        }
        else
        {
            // Record normal outgoing GCR data.
            data[position] = value;
            sync[position] = 0;
        }

        gcrDirty = true;
        ++gcrBytesWritten;

        return true;
}

bool D4040Mechanism::hasUncommittedGCR() const
{
    return gcrDirty || !dirtyTracks.empty();
}

void D4040Mechanism::cacheActiveTrack()
{
    if (!gcrDirty || !gcrValid || loadedGCRTrack == 0)
        return;

    // Preserve the complete modified GCR stream.
    dirtyTracks[loadedGCRTrack] = gcrTrack;

    gcrDirty = false;
}

bool D4040Mechanism::restoreCachedTrack(uint8_t trackNumber)
{
    const auto it = dirtyTracks.find(trackNumber);

    if (it == dirtyTracks.end())
        return false;

    const auto& data = it->second.getTrackData();
    const auto& sync = it->second.getSyncMap();

    if (data.empty() || sync.size() != data.size())
        return false;

    gcrTrack = it->second;

    loadedGCRTrack = trackNumber;
    gcrValid = true;
    gcrDirty = true;
    gcrPos = 0;

    return true;
}

void D4040Mechanism::clearDirtyTrackCache()
{
    dirtyTracks.clear();

    gcrDirty = false;
    gcrBytesWritten = 0;
    gcrSyncBytesWritten = 0;
}

void D4040Mechanism::insertDisk(
    std::unique_ptr<Disk> image,
    const std::string& path)
{
    // The caller is responsible for saving the outgoing disk.
    // This function only installs the replacement.
    if (!image)
        return;

    diskImage = std::move(image);

    diskInserted = true;
    diskPath = path;
    writeProtected = diskImage->isWriteProtected();

    // The replacement disk must not inherit the previous
    // disk's modified GCR tracks.
    clearDirtyTrackCache();

    // Reset media-dependent signals.
    syncDetected = false;
    byteReady = false;

    readLatch = 0xFF;
    writeLatch = 0xFF;

    // Invalidate the previous GCR stream.
    gcrTrack.clear();

    gcrPos = 0;
    loadedGCRTrack = 0;
    gcrValid = false;
    gcrCycleAccumulator = 0;

    // Reset the GCR decoder.
    gcrEncodedCount = 0;
    gcrDecodedIndex = 4;
}

void D4040Mechanism::removeDisk()
{
    // The caller must verify that pending writes were
    // successfully saved before calling this function.
    clearDirtyTrackCache();

    diskImage.reset();

    diskInserted = false;
    diskPath.clear();
    writeProtected = false;

    syncDetected = false;
    byteReady = false;

    readLatch = 0xFF;
    writeLatch = 0xFF;

    gcrTrack.clear();

    gcrPos = 0;
    loadedGCRTrack = 0;
    gcrValid = false;
    gcrCycleAccumulator = 0;

    gcrEncodedCount = 0;
    gcrDecodedIndex = 4;
}

bool D4040Mechanism::hasDisk() const
{
    return diskImage != nullptr;
}

bool D4040Mechanism::isWriteProtected() const
{
    return writeProtected;
}

const std::string& D4040Mechanism::getDiskPath() const
{
    return diskPath;
}

Disk* D4040Mechanism::getDiskImage()
{
    return diskImage.get();
}

const Disk* D4040Mechanism::getDiskImage() const
{
    return diskImage.get();
}

void D4040Mechanism::setMotorCommand(bool enabled)
{
    motorCommandOn = enabled;
}

bool D4040Mechanism::getMotorCommand() const
{
    return motorCommandOn;
}

bool D4040Mechanism::isMotorRunning() const
{
    return motorOn;
}

uint8_t D4040Mechanism::getTrack() const
{
    return track;
}

uint8_t D4040Mechanism::getSector() const
{
    return sector;
}

uint8_t D4040Mechanism::getHalfTrack() const
{
    return halfTrack;
}

uint8_t D4040Mechanism::getStepperPhase() const
{
    return stepperPhase;
}

uint32_t D4040Mechanism::getMotorSpinDownCycles() const
{
    return motorSpinDownCycles;
}

void D4040Mechanism::advanceMotor(uint32_t cycles)
{
    static constexpr uint32_t MOTOR_SPINDOWN_CYCLES = 200000;

    if (motorCommandOn)
    {
        // Motor power is enabled.
        // The spindle runs immediately.
        motorOn = true;

        // Keep the full spin-down interval available
        // while motor power remains enabled.
        motorSpinDownCycles = MOTOR_SPINDOWN_CYCLES;

        return;
    }

    // Motor power is disabled.
    if (!motorOn)
    {
        motorSpinDownCycles = 0;
        return;
    }

    // Motor continues rotating until spin-down finishes.
    if (cycles >= motorSpinDownCycles)
    {
        motorSpinDownCycles = 0;
        motorOn = false;
    }
    else
    {
        motorSpinDownCycles -= cycles;
    }
}

bool D4040Mechanism::isSyncDetected() const
{
    return syncDetected;
}

void D4040Mechanism::setSyncDetected(bool detected)
{
    syncDetected = detected;
}

bool D4040Mechanism::isByteReady() const
{
    return byteReady;
}

void D4040Mechanism::setByteReady(bool ready)
{
    byteReady = ready;
}

uint8_t D4040Mechanism::getReadLatch() const
{
    return readLatch;
}

void D4040Mechanism::setReadLatch(uint8_t value)
{
    readLatch = value;
}

uint8_t D4040Mechanism::getWriteLatch() const
{
    return writeLatch;
}

void D4040Mechanism::setWriteLatch(uint8_t value)
{
    writeLatch = value;
}

GCRTrackStream& D4040Mechanism::getGCRTrack()
{
    return gcrTrack;
}

const GCRTrackStream& D4040Mechanism::getGCRTrack() const
{
    return gcrTrack;
}

size_t D4040Mechanism::getGCRPosition() const
{
    return gcrPos;
}

void D4040Mechanism::setGCRPosition(size_t position)
{
    gcrPos = position;
}

uint8_t D4040Mechanism::getLoadedGCRTrack() const
{
    return loadedGCRTrack;
}

void D4040Mechanism::setLoadedGCRTrack(uint8_t track)
{
    loadedGCRTrack = track;
}

bool D4040Mechanism::isGCRValid() const
{
    return gcrValid;
}

void D4040Mechanism::setGCRValid(bool valid)
{
    gcrValid = valid;
}

uint32_t D4040Mechanism::getGCRCycleAccumulator() const
{
    return gcrCycleAccumulator;
}

void D4040Mechanism::setGCRCycleAccumulator(uint32_t cycles)
{
    gcrCycleAccumulator = cycles;
}

bool D4040Mechanism::isGCRDirty() const
{
    return gcrDirty;
}

void D4040Mechanism::setGCRDirty(bool dirty)
{
    gcrDirty = dirty;
}

uint32_t D4040Mechanism::getGCRBytesWritten() const
{
    return gcrBytesWritten;
}

uint32_t D4040Mechanism::getGCRSyncBytesWritten() const
{
    return gcrSyncBytesWritten;
}

void D4040Mechanism::resetGCRWriteStatistics()
{
    gcrBytesWritten = 0;
    gcrSyncBytesWritten = 0;
}

void D4040Mechanism::resetGCRDecoder()
{
    gcrEncodedCount = 0;
    gcrDecodedIndex = 4;
}

bool D4040Mechanism::decodeGCRByte(uint8_t encodedByte, const GCRCodec& codec, uint8_t& decodedByte)
{
    // Collect five encoded GCR bytes.
    gcrEncodedBuffer[gcrEncodedCount++] = encodedByte;

    if (gcrEncodedCount == 5)
    {
        // Decode five physical GCR bytes into
        // four bytes visible to the controller.
        const bool decoded = codec.decode5Bytes(gcrEncodedBuffer, gcrDecodedBuffer);

        gcrEncodedCount = 0;

        if (decoded)
            gcrDecodedIndex = 0;
        else
            gcrDecodedIndex = 4;
    }

    // Deliver the next available decoded byte.
    if (gcrDecodedIndex < 4)
    {
        decodedByte = gcrDecodedBuffer[gcrDecodedIndex++];
        return true;
    }

    return false;
}

const GCRTrackStream* D4040Mechanism::findDirtyTrack(uint8_t track) const
{
    const auto it = dirtyTracks.find(track);

    if (it == dirtyTracks.end())
        return nullptr;

    return &it->second;
}

const std::unordered_map<uint8_t, GCRTrackStream>& D4040Mechanism::getDirtyTracks() const
{
    return dirtyTracks;
}
