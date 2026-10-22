// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef IEEE488DRIVE_H
#define IEEE488DRIVE_H

#include <cstdint>
#include <string>
#include "IEEE488/IEEE488Device.h"

class IEEE488Drive : public IEEE488Device
{
    public:
        IEEE488Drive();
        virtual ~IEEE488Drive();

        void reset() override;

        void tick(uint64_t cycles) override = 0;

        virtual bool insertDisk(uint8_t mechanism, const std::string& path) = 0;
        virtual void ejectDisk(uint8_t mechanism) = 0;
};

#endif // IEEE488DRIVE_H
