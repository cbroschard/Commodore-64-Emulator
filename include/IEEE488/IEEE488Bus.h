// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef IEEE488BUS_H
#define IEEE488BUS_H

#include <cstdint>
#include <vector>

class IEEE488Device;

class IEEE488Bus
{
    public:
        using DeviceId = uint32_t;

        IEEE488Bus();
        ~IEEE488Bus();

        void reset();

        // Device Management
        bool attachDevice(DeviceId id, IEEE488Device* device);
        void detachDevice(DeviceId id);
        bool isDeviceAttached(DeviceId id) const;

        enum class Signal
        {
            DAV,
            NRFD,
            NDAC,
            ATN,
            EOI,
            IFC,
            REN,
            SRQ
        };

        bool setSignal(DeviceId id, Signal signal, bool level);
        bool getSignal(Signal signal) const;

        bool setDIO(DeviceId id, uint8_t value);
        uint8_t getDIO() const;

    private:
        struct AttachedDevice
        {
            DeviceId id = 0;
            IEEE488Device* device = nullptr;

            uint8_t dio = 0xFF;

            bool dav  = true;
            bool nrfd = true;
            bool ndac = true;
            bool atn  = true;
            bool eoi  = true;
            bool ifc  = true;
            bool ren  = true;
            bool srq  = true;
        };

        std::vector<AttachedDevice> devices;

        uint8_t dio; // 8 data lines dio1-dio8

        bool dav;   // Data valid
        bool nrfd;  // Not ready for data
        bool ndac;  // Not data accepted
        bool atn;   // Attention
        bool eoi;   // End or idnetify
        bool ifc;   // Interface clear
        bool ren;   // Remote enable
        bool srq;   // Service request

        void resolveSignals();
        AttachedDevice* findDevice(DeviceId id);
};

#endif // IEEE488BUS_H
