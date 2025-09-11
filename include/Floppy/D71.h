// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef D71_H
#define D71_H

#include "CBMImage.h"

class D71 : public CBMImage
{
    public:
        D71();
        virtual ~D71();

        bool loadDisk(const std::string& filePath) override;
        bool saveDisk(const std::string& filePath) override;

        // Getter for D1541/D1571 access
        const std::vector<uint8_t>& getRawImage() const override;

        struct TrackSectorInfo
        {
            uint8_t startTrack;
            uint8_t endTrack;
            uint16_t numSectors;
        };

    protected:

    private:

        // Disk size constants for D71
        static constexpr size_t D71_STANDARD_SIZE_70 = 349696; // 70 tracks (35+35)
        static constexpr size_t D71_EXTENDED_SIZE_80 = 393216; // 80 tracks (40+40)

        // Helpers for disk access
        uint16_t getSectorsForTrack(uint8_t track) override;
        bool validateDiskImage() override;
};

#endif // D71_H
