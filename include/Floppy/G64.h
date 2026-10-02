// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef G64_H
#define G64_H

#include "Floppy/Disk.h"

class G64 : public Disk
{
    public:
        G64();
        ~G64() override;

        // Loading/saving
        bool loadDisk(const std::string& filePath) override;
        bool saveDisk(const std::string& filePath) override;

        // Raw track access
        size_t getTrackCount() const;
        bool hasTrack(size_t index) const;

        const std::vector<uint8_t>& getTrackData(size_t index) const;
        const std::vector<uint8_t>& getTrackSpeedZones(size_t index) const;

    protected:
        const std::vector<uint8_t>& getRawImage() const override;
        bool validateDiskImage() override;

    private:
        struct G64Header
        {
            uint8_t version = 0;
            uint8_t trackCount = 0;
            uint16_t maxTrackSize = 0;

            std::vector<uint32_t> trackOffsets;
            std::vector<uint32_t> speedEntries;
        };

        struct G64Track
        {
            std::vector<uint8_t> data;
            std::vector<uint8_t> speedZones;
            bool present = false;
        };

        G64Header header;
        std::vector<G64Track> tracks;

        static uint16_t readLE16(const std::vector<uint8_t>& data, size_t offset);
        static uint32_t readLE32(const std::vector<uint8_t>& data, size_t offset);

        bool parseHeader();
        bool parseTracks();
};

#endif // G64_H
