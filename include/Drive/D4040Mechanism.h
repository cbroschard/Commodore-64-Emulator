// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef D4040MECHANISM_H_INCLUDED
#define D4040MECHANISM_H_INCLUDED

#include <cstdint>
#include <memory>
#include <string>
#include "Floppy/DiskFactory.h"

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

    // Disk signals
    bool writeProtected = false;
    bool syncDetected = false;
    bool byteReady = false;

    // Data transfer
    uint8_t readLatch = 0xFF;
    uint8_t writeLatch = 0xFF;

    void reset()
    {
        motorOn = false;
        stepperPhase = 0;

        syncDetected = false;
        byteReady = false;

        readLatch = 0xFF;
        writeLatch = 0xFF;
    }
};

#endif // D4040MECHANISM_H_INCLUDED
