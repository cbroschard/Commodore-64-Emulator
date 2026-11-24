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

class IDriveHardwareProvider
{
public:
    virtual ~IDriveHardwareProvider() = default;

    virtual std::vector<DriveHardwareInfo>
        enumerateDriveHardware() const = 0;
};

#endif // IDRIVEHARDWAREPROVIDER_H
