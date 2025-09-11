// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef D64_H
#define D64_H

#include "CBMImage.h"

class D64 : public CBMImage
{
    public:
        D64();
        virtual ~D64();

        // Loading/saving
        bool loadDisk(const std::string& filePath) override;
        bool saveDisk(const std::string& filePath) override;

        // Getter for D1541 access
        const std::vector<uint8_t>& getRawImage() const override;

        struct TrackSectorInfo
        {
            uint8_t startTrack;
            uint8_t endTrack;
            uint16_t numSectors;
        };

    protected:

    private:

        // Disk size constants
        static constexpr size_t D64_STANDARD_SIZE_35 = 174848;
        static constexpr size_t D64_STANDARD_SIZE_40 = 196608;

        // Helpers for disk access
        uint16_t getSectorsForTrack(uint8_t track) override;

        // Run multiple validations against the file to ensure it's a valid image
        bool validateDiskImage() override;
};

#endif // D64_H
