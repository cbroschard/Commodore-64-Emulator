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
#include "IEEE488/IEEE488Device.h"
#include "IRQLine.h"

class D2031 : public Drive
{
    public:
        D2031(int deviceNumber, const std::string& loRom, const std::string& hiRom);
        ~D2031();

        enum class D2031Model : uint8_t
        {
            HighProfile,
            LowProfile
        };

        void reset() override;

        bool attachIEEE488Bus(IEEE488Bus* ieee488Bus);

        // Drive Interface
        inline bool isWriteProtected() const { return diskWriteProtected; }

        // Raw GCR write mechanics
        void setDiskWriteGate(bool enabled);
        void onVIA2PortARead(uint8_t value);

        inline bool getIEEE488Signal(IEEE488Bus::Signal signal) const { return ieee488Device.getBusSignal(signal); }

        // ML Monitor
        inline bool hasCIA()  const override { return false; }
        inline bool hasVIA1() const override { return true;  }
        inline bool hasVIA2() const override { return true;  }
        inline bool hasFDC()  const override { return false; }
        inline bool isDrive() const override { return true;  }
        inline const CPU* getDriveCPU() const override { return &driveCPU; }
        inline CPU* getDriveCPU() override { return &driveCPU; }
        inline const D2031Memory* getMemory() const override { return &d2031mem; }
        inline D2031Memory* getMemory() override { return &d2031mem; }
        inline const D2031VIA* getVIA1() const override { return &d2031mem.getVIA1(); }
        inline const D2031VIA* getVIA2() const override { return &d2031mem.getVIA2(); }
        inline DriveStatus getDriveStatus() const override { return status; }
        const char* getDriveTypeName() const noexcept override { return "2031"; }

    private:
        D2031Bus d2031Bus;
        D2031Memory d2031mem;
        CPU driveCPU;
        GCRTrackStream gcrTrack;
        GCRCodec gcrCodec;
        IEEE488Device ieee488Device;
        IRQLine IRQ;

        // Status tracking
        DriveError  lastError;
        DriveStatus status;

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

        // UI Activity
        uint8_t uiTrack;
        uint8_t uiSector;
        bool uiLedWasOn;

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
