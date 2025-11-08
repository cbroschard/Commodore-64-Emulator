// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef D1571_H
#define D1571_H

#include <algorithm>
#include <cstring>
#include "IECBUS.h"
#include "CPU.h"
#include "IRQLine.h"
#include "Drive/D1571Memory.h"
#include "Drive/Drive.h"
#include "Floppy/Disk.h"
#include "Floppy/DiskFactory.h"

class D1571 : public Drive
{
    public:
        D1571(int deviceNumber, const std::string& fileName);
        virtual ~D1571();

        inline void attachBusInstance(IECBUS* bus) { this->bus = bus; }

        // Advance drive via tick method
        void tick() override;

        // Compatibility check
        bool canMount(DiskFormat fmt) const override;

        // IRQ handling
        void updateIRQ();

        // Floppy image handling
        bool mountDisk(const std::string& path);
        void unmountDisk();
        bool readSector(uint8_t track, uint8_t sector, uint8_t* buffer, size_t length);

    protected:
        bool motorOn;

    private:

        // Drive chips
        CPU driveCPU;
        D1571Memory d1571Mem;
        IRQLine IRQ;

        // Floppy factory
        std::unique_ptr<Disk> diskImage;

        // Non-owning pointers
        IECBUS* bus;

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
