// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef NIB_H
#define NIB_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "Floppy/Disk.h"

class NIB : public Disk
{
    public:
        NIB();
        ~NIB();

        static constexpr size_t NIB_HEADER_SIZE  = 0x100;
        static constexpr size_t NIB_TRACK_LENGTH = 0x2000;

        struct NIBTrack
        {
            bool present = false;

            std::vector<uint8_t> data;
            std::vector<uint8_t> speedZones;
        };

        bool loadDisk(const std::string& filePath) override;
        bool saveDisk(const std::string& filePath) override;

        inline DiskTrackModel getTrackModel() const override { return DiskTrackModel::RawGCR; }

        inline bool supportsRawTracks() const override { return true; }

        bool readRawTrack(size_t halfTrack, GCRTrackStream& outTrack) const override;
        bool writeRawTrack(size_t halfTrack, const GCRTrackStream& track) override;

        inline bool hasRawTrack(size_t halfTrack) const override { return halfTrack < tracks.size() && tracks[halfTrack].present; }
        inline size_t getHalfTrackCount() const override { return tracks.size(); }

    protected:
        const std::vector<uint8_t>& getRawImage() const override;
        bool validateDiskImage() override;

    private:
        std::vector<NIBTrack> tracks;
};

#endif // NIB_H
