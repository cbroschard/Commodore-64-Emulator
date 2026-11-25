// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.

#ifndef IDRIVEHARDWAREPROVIDER_H
#define IDRIVEHARDWAREPROVIDER_H

#include "Drive/DriveHardware.h"

class CPU;

class IDriveHardwareProvider
{
public:
    virtual ~IDriveHardwareProvider() = default;

    virtual std::vector<DriveHardwareInfo> enumerateDriveHardware() const = 0;

    // Return a CPU by its hardware component ID.
    // Drives without the requested CPU return nullptr.
    virtual CPU* getHardwareCPU(uint32_t id) { return nullptr; }
};

#endif // IDRIVEHARDWAREPROVIDER_H
