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
        struct G64Track
        {
            std::vector<uint8_t> data;
            std::vector<uint8_t> speedZones;
            bool present = false;
        };

        std::vector<G64Track> tracks;

};

#endif // G64_H
