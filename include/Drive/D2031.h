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
#include "Drive/IDriveIndicatorView.h"
#include "Drive/IDrivePositionView.h"
#include "Drive/IDriveUIView.h"
#include "GCR/GCRCodec.h"
#include "GCR/GCRTrackStream.h"
#include "IEEE488/IEEE488Device.h"
#include "IRQLine.h"

class D2031 : public Drive, public IDriveIndicatorView, public IDrivePositionView, public IDriveUiView
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
        void onIEEE488BusChanged(const IEEE488Bus::BusState& oldState, const IEEE488Bus::BusState& newState);

        // State Management
        void saveState(StateWriter& wrtr) const override;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr) override;

        // Drive execution
        void tick(uint32_t cycles) override;
        void updateIRQ() override;

        // Drive Interface
        inline bool isWriteProtected() const { return diskWriteProtected; }

        void setDiskWriteGate(bool enabled);
        void onVIA2PortARead(uint8_t value);
        void onVIA2PortAWrite(uint8_t value, uint8_t ddrA);

        void onStepperPhaseChange(uint8_t oldPhase, uint8_t newPhase);
        void setDensityCode(uint8_t code);

        // Drive indicators
        void getDriveIndicators(std::vector<IDriveIndicatorView::Indicator>& out) const override;

        // Disk management
        void loadDisk(const std::string& path) override;
        void unloadDisk() override;

        // Motor control
        void startMotor() override;
        void stopMotor() override;

        // Disk persistence
        void flushAndSaveDisk() override;

        // CPU bus access
        inline CPUBus* getDriveBus() override { return &d2031Bus; }
        inline const CPUBus* getDriveBus() const override { return &d2031Bus; }

        // Mounted disk information
        inline const std::string& getLoadedDiskName() const override { return loadedDiskName; }
        inline std::string getCurrentDiskPath() const override { return loadedDiskName; }

        // Motor state
        inline bool isMotorOn() const override { return motorOn; }

        // Supported disk formats
        inline bool canMount(DiskFormat format) const override { return format == DiskFormat::D64 || format == DiskFormat::G64 ||
                   format == DiskFormat::NIB; }

        inline bool getIEEE488Signal(IEEE488Bus::Signal signal) const { return ieee488Device.getBusSignal(signal); }
        inline bool setIEEE488Signal(IEEE488Bus::Signal signal, bool level) { return ieee488Device.setBusSignal(signal, level); }
        inline uint8_t getIEEE488BusDIO() const { return ieee488Device.getBusDIO(); }
        inline bool setIEEE488BusDIO(uint8_t value) { return ieee488Device.setBusDIO(value); }

        // Drive model
        inline DriveModel getDriveModel() const override { return DriveModel::D2031; }

        // Current disk information
        inline bool isDiskLoaded() const override { return diskLoaded; }
        inline uint8_t getCurrentTrack() const override { return static_cast<uint8_t>(currentTrack + 1); }
        inline uint8_t getCurrentSector() const override { return currentSector; }

        // UI information
        inline const char* getDriveModelName() const override { return "Commodore 2031"; }

        inline bool hasDiskInserted() const override { return diskLoaded; }
        inline std::string getMountedImagePath() const override { return loadedDiskName; }

        // Position display
        inline bool hasTrackSector() const override { return diskLoaded; }

        inline int getTrack() const override { return static_cast<int>(currentTrack) + 1; }
        inline int getSector() const override { return static_cast<int>(currentSector); }

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

        // =========================================================
        // Temporary IEC Compatibility
        //
        // D2031 uses IEEE-488, not serial IEC.
        //
        // These overrides are required because Drive currently
        // inherits from Peripheral.
        //
        // TODO: Refactor Drive/Peripheral inheritance later.
        // =========================================================

        // Peripheral IEC callbacks
        void clkChanged(bool) override {}

        void onListen() override {}
        void onUnListen() override {}

        void onTalk() override {}
        void onUnTalk() override {}

        void onSecondaryAddress(uint8_t) override {}

        // Peripheral SRQ handling
        bool isSRQAsserted() const override
        {
            return false;
        }

        void setSRQAsserted(bool) override {}

        // Drive IEC signal handling
        bool getAtnLineLow() const override
        {
            return false;
        }

        bool getClkLineLow() const override
        {
            return false;
        }

        bool getDataLineLow() const override
        {
            return false;
        }

        bool getSRQAsserted() const override
        {
            return false;
        }

        void forceSyncIEC() override {}

    protected:
        bool motorOn;

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
        inline int stepIndex(uint8_t p) const { return (p & 0x03) * 2; }
        bool gcrTick();
        void acceptGCRWriteByte(uint8_t value);

        void rebuildSyncMapForCurrentTrack();

        void loadCurrentRawTrackFromCacheOrBuild();
        void saveCurrentRawTrackToCache();
        void invalidateRawGcrCache();

        void resetForMediaChange();

        void gcrAdvance(uint32_t dc);
        int cyclesPerByteFromDensity(uint8_t code) const;

        void sampleHeaderAtCurrentPosition(size_t pos);

        void rebuildGCRTrackStream();
        void flushAllDirtyRawTracksToImage();
        void flushCurrentRawTrackToImage();

        bool decodeRawSectorFromCurrentTrack(uint8_t track, uint8_t sector, std::vector<uint8_t>& outSector);
        size_t findHeaderPosForSector(uint8_t track, uint8_t sector) const;

        CBMImage* getCBMImage();
        const CBMImage* getCBMImage() const;
};

#endif // D2031_H
