// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef FLOPPYCONTROLLERHOST_H
#define FLOPPYCONTROLLERHOST_H

#include <cstdint>

class FloppyControllerHost
{
    public:
        FloppyControllerHost();
        virtual ~FloppyControllerHost();

        virtual bool fdcReadSector(uint8_t track, uint8_t sector, uint8_t* buffer, size_t length) = 0;
        virtual bool fdcWriteSector(uint8_t track, uint8_t sector, const uint8_t* buffer, size_t length) = 0;
        virtual bool fdcIsWriteProtected() const = 0;

    protected:

    private:
};

#endif // FLOPPYCONTROLLERHOST_H
