// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef D2031_H
#define D2031_H

#include <cstdint>
#include <vector>
#include "CPU.h"
#include "Drive/D2031Bus.h"
#include "Drive/D2031Memory.h"
#include "Drive/Drive.h"
#include "GCR/GCRCodec.h"
#include "GCR/GCRTrackStream.h"
#include "IRQLine.h"

class D2031 : public Drive
{
    public:
        D2031();
        ~D2031();

        enum class D2031Model : uint8_t
        {
            HighProfile,
            LowProfile
        };

        void reset() override;

        // Drive Interface
        inline bool isWriteProtected() const { return diskWriteProtected; }

        // Raw GCR write mechanics
        void setDiskWriteGate(bool enabled);
        void onVIA2PortARead(uint8_t value);

    private:
        D2031Bus d2301Bus;
        D2031Memory d2301mem;
        CPU driveCPU;
        GCRTrackStream gcrTrack;
        GCRCodec gcrCodec;
        IRQLine IRQ;

        std::vector<uint8_t> writeGcrBuffer;
        std::vector<uint8_t> readGcrHeaderProbe;

        // Raw GCR track cache. Once written, the raw track is authoritative
        // until we explicitly flush/decode it back to the sector image.
        std::array<std::vector<uint8_t>, 35> rawGcrTrackCache;
        std::array<std::vector<uint8_t>, 35> rawGcrSyncCache;
        std::array<std::vector<uint8_t>, 35> rawGcrSectorCache;
        std::array<bool, 35> rawGcrTrackValid{};
        std::array<bool, 35> rawGcrTrackDirty{};
        std::vector<uint8_t> gcrWrittenMask;

        // Track sectors for UI
        std::vector<uint8_t> gcrSectorAtPos;

        // Floppy Image
        std::string loadedDiskName;
        bool    diskLoaded;
        bool    diskWriteProtected;

        uint8_t lastHeaderTrack;
        uint8_t lastHeaderSector;

        bool    haveLastHeader;
        size_t  lastHeaderPos;
        bool    lastHeaderValid;
        bool    diskWriteGate;
        size_t  pendingWritePos;
        bool    pendingWritePosValid;
        bool    trackModifiedByWrite;
        int     writeSyncRun;
        bool    writeAfterSync;
        int     writeGapRun;

        int     readSyncRun;
        bool    readAfterSync;

        int     halfTrackPos;
        uint8_t currentTrack;
        uint8_t currentSector;
        uint8_t densityCode; // 0..3

        // GCR
        int  gcrBitCounter; // Used to rate limit bits
        size_t gcrPos;
        bool gcrDirty;

        uint8_t lastRomHeaderTrack;
        uint8_t lastRomHeaderSector;
        bool lastRomHeaderValid;
        size_t lastRomHeaderPos;
        uint64_t lastRomHeaderCycle;

        // Helpers
        void rebuildSyncMapForCurrentTrack();
        void saveCurrentRawTrackToCache();
        void invalidateRawGcrCache();
};

#endif // D2031_H
