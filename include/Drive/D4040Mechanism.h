// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef D4040MECHANISM_H_INCLUDED
#define D4040MECHANISM_H_INCLUDED

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include "Floppy/DiskFactory.h"
#include "GCR/GCRTrackStream.h"

struct D4040Mechanism
{
    // Media
    bool diskInserted = false;
    std::string diskPath;
    std::unique_ptr<Disk> diskImage;

    // Electrical command from the controller VIA.
    bool motorCommandOn = false;

    // Physical spindle state.
    bool motorOn = false;

    // Emulated controller cycles remaining before
    // the spindle stops after motor power is removed.
    uint32_t motorSpinDownCycles = 0;

    uint8_t track = 18;
    uint8_t sector = 0;
    uint8_t stepperPhase = 0;

    // Physical head position: track number * 2.
    // Track 1 = 2, track 18 = 36, track 35 = 70.
    uint8_t halfTrack = 36;

    // Whether we have observed an initial stepper phase.
    bool stepperInitialized = false;

    // Disk signals
    bool writeProtected = false;
    bool syncDetected = false;
    bool byteReady = false;

    // Data transfer
    uint8_t readLatch = 0xFF;
    uint8_t writeLatch = 0xFF;

    // Per-mechanism GCR state
    GCRTrackStream gcrTrack;

    // GCR read decoder state.
    //
    // The physical track contains five GCR bytes for
    // every four decoded data bytes.
    //
    // The controller receives decoded bytes through VIA Port A.
    uint8_t gcrEncodedBuffer[5] = {};
    uint8_t gcrEncodedCount = 0;

    uint8_t gcrDecodedBuffer[4] = {};
    uint8_t gcrDecodedIndex = 4;

    // Only modified tracks are cached.
    // Key: physical whole-track number (1-35).
    std::unordered_map<uint8_t, GCRTrackStream> dirtyTracks;

    bool hasUncommittedGCR() const
    {
        return gcrDirty || !dirtyTracks.empty();
    }

    void cacheActiveTrack()
    {
        if (!gcrDirty || !gcrValid ||
            loadedGCRTrack == 0)
            return;

        // Copy both recorded bytes and sync metadata.
        dirtyTracks[loadedGCRTrack] = gcrTrack;
        gcrDirty = false;
    }

    bool restoreCachedTrack(uint8_t trackNumber)
    {
        auto it = dirtyTracks.find(trackNumber);

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

    void clearDirtyTrackCache()
    {
        dirtyTracks.clear();
        gcrDirty = false;
        gcrBytesWritten = 0;
        gcrSyncBytesWritten = 0;
    }

    size_t gcrPos = 0;

    // Track number currently represented by gcrTrack.
    uint8_t loadedGCRTrack = 0;

    // Indicates whether the stream matches the mounted
    // disk and current physical head position.
    bool gcrValid = false;

    // Accumulated emulated cycles since the last GCR byte.
    uint32_t gcrCycleAccumulator = 0;

    // In-memory GCR track has been modified.
    bool gcrDirty = false;

    // Number of GCR bytes recorded on the current track.
    uint32_t gcrBytesWritten = 0;

    uint32_t gcrSyncBytesWritten = 0;

    void reset()
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
        gcrEncodedCount = 0;
        gcrDecodedIndex = 4;
        gcrDirty = false;
        gcrBytesWritten = 0;
        gcrSyncBytesWritten = 0;
    }

    void updateStepperPhase(uint8_t newPhase)
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

    bool writeGCRByte(size_t position, uint8_t value,  bool writeSync = false)
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
};

#endif // D4040MECHANISM_H_INCLUDED
