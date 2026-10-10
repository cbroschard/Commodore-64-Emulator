// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef G71_H
#define G71_H

#include <cstdint>
#include <vector>
#include "Floppy/Disk.h"

class G71 : public Disk
{
    public:
        G71();
        ~G71() override;

        // Loading/saving
        bool loadDisk(const std::string& filePath) override;
        bool saveDisk(const std::string& filePath) override;

        inline DiskTrackModel getTrackModel() const override { return DiskTrackModel::RawGCR; }

        inline bool supportsRawTracks() const override { return true; }

        bool readRawTrack(size_t halfTrack, GCRTrackStream& outTrack) const override;
        bool writeRawTrack(size_t halfTrack, const GCRTrackStream& track) override;

        inline bool hasRawTrack(size_t halfTrack) const override { return hasTrack(halfTrack); }
        inline size_t getHalfTrackCount() const override { return getTrackCount(); }

        // Raw track access
        size_t getTrackCount() const;
        bool hasTrack(size_t index) const;

        const std::vector<uint8_t>& getTrackData(size_t index) const;
        const std::vector<uint8_t>& getTrackSpeedZones(size_t index) const;

        bool setTrackData(size_t index, const std::vector<uint8_t>& data);

    protected:
        const std::vector<uint8_t>& getRawImage() const override;
        bool validateDiskImage() override;

    private:
        struct G71Header
        {
            uint8_t version = 0;
            uint8_t trackCount = 0;
            uint16_t maxTrackSize = 0;

            std::vector<uint32_t> trackOffsets;
            std::vector<uint32_t> speedEntries;
        };

        struct G71Track
        {
            std::vector<uint8_t> data;
            std::vector<uint8_t> speedZones;
            bool present = false;
        };

        G71Header header;
        std::vector<G71Track> tracks;

        static uint16_t readLE16(const std::vector<uint8_t>& data, size_t offset);
        static uint32_t readLE32(const std::vector<uint8_t>& data, size_t offset);

        bool parseHeader();
        bool parseTracks();
};

#endif // G71_H
