// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef GCRTRACKSTREAM_H
#define GCRTRACKSTREAM_H

#include <cstdint>
#include <cstddef>
#include <vector>

class GCRTrackStream
{
    public:
        GCRTrackStream();
        ~GCRTrackStream();

        void clear();

        void setTrackData(const std::vector<uint8_t>& data);
        void setSpeedZones(const std::vector<uint8_t>& zones);

        const std::vector<uint8_t>& getTrackData() const;
        std::vector<uint8_t>& getTrackData();

        const std::vector<uint8_t>& getSyncMap() const;
        std::vector<uint8_t>& getSyncMap();

        const std::vector<uint8_t>& getSpeedZones() const;

        bool empty() const;
        size_t size() const;

        void rebuildSyncMap();

    private:
        std::vector<uint8_t> trackData;
        std::vector<uint8_t> syncMap;
        std::vector<uint8_t> speedZones;
};

#endif // GCRTRACKSTREAM_H
