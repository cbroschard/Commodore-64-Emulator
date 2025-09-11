// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef D81_H
#define D81_H

#include "CBMIMage.h"

class D81 : public CBMImage
{
    public:
        D81();
        virtual ~D81();

        // Loading/Saving the disk image
        bool loadDisk(const std::string& filePath) override;
        bool saveDisk(const std::string& filePath) override;

        // Getter for D1581 access
        const std::vector<uint8_t>& getRawImage() const override;

    protected:

    private:

        static constexpr size_t D81_HEADER_SIZE        = 0x90; // 144-byte header
        static constexpr int    D81_TRACK_COUNT        = 80;
        static constexpr int    D81_SECTORS_PER_TRACK  = 40;

        // Helpers for disk access
        uint16_t getSectorsForTrack(uint8_t track) override;

        // Image validators
        bool validateDiskImage() override;
};

#endif // D81_H
