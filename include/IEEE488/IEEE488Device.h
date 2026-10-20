// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef IEEE488DEVICE_H
#define IEEE488DEVICE_H

#include <cstdint>

class IEEE488Device
{
    public:
        IEEE488Device();
        virtual ~IEEE488Device();

        IEEE488Device(const IEEE488Device&) = delete;
        IEEE488Device& operator=(const IEEE488Device&) = delete;

        virtual void reset();
        virtual void tick(uint64_t cycles);
};

#endif // IEEE488DEVICE_H
