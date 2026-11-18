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
#include "Floppy/DiskFactory.h"
#include "GCR/GCRTrackStream.h"

struct D4040Mechanism
{
    // Media
    bool diskInserted = false;
    std::string diskPath;
    std::unique_ptr<Disk> diskImage;

    // Motor and head
    bool motorOn = false;
    uint8_t track = 18;
    uint8_t sector = 0;
    uint8_t stepperPhase = 0;

    // Physical head position: track number * 2.
    // Track 1 = 2, track 18 = 36, track 35 = 70.
    uint8_t halfTrack = 36;

    // Whether we have observed an initial stepper phase.
    bool stepperInitialized = false;

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

    // Disk signals
    bool writeProtected = false;
    bool syncDetected = false;
    bool byteReady = false;

    // Data transfer
    uint8_t readLatch = 0xFF;
    uint8_t writeLatch = 0xFF;

    // Per-mechanism GCR state
    GCRTrackStream gcrTrack;

    size_t gcrPos = 0;

    // Track number currently represented by gcrTrack.
    uint8_t loadedGCRTrack = 0;

    // Indicates whether the stream matches the mounted
    // disk and current physical head position.
    bool gcrValid = false;

    // Accumulated emulated cycles since the last GCR byte.
    uint32_t gcrCycleAccumulator = 0;

    void reset()
    {
        motorOn = false;
        stepperPhase = 0;

        syncDetected = false;
        byteReady = false;

        readLatch = 0xFF;
        writeLatch = 0xFF;

        halfTrack = static_cast<uint8_t>(track * 2);
        stepperInitialized = false;

        gcrTrack.clear();
        gcrPos = 0;
        loadedGCRTrack = 0;
        gcrValid = false;
        gcrCycleAccumulator = 0;
    }
};

#endif // D4040MECHANISM_H_INCLUDED
