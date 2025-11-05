// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef D1571_H
#define D1571_H

#include "Drive.h"
#include "Floppy/D64.h"
#include "Floppy/D71.h"

class D1571 : public Drive
{
    public:
        D1571(int deviceNumber);
        virtual ~D1571();

        // Advance drive via tick method
        void tick() override;

        // Compatibility check
        bool canMount(DiskFormat fmt) const override;

    protected:
        bool motorOn;
    private:

        void reset() override;

        // Floppy Image
        std::string loadedDiskName;
        bool diskLoaded;

        // Status tracking
        DriveError lastError;
        DriveStatus status;

        // Drive geometry
        uint8_t currentTrack;
        uint8_t currentSector;

};

#endif // D1571_H
