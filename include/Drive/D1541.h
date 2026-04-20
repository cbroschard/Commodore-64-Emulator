// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef D1541_H
#define D1541_H

#include "Drive/Drive.h"
#include "Drive/D1541Memory.h"
#include "Drive/IDriveIndicatorView.h"
#include "Drive/IDrivePositionView.h"
#include "Drive/IDriveUIView.h"
#include "Floppy/Disk.h"
#include "Floppy/DiskFactory.h"
#include "Drive/D1541VIA.h"
#include <array>
#include <vector>

class D1541 : public Drive, public IDriveIndicatorView, public IDrivePositionView, public IDriveUiView
{
    public:
        D1541(int deviceNumber, const std::string& loRom, const std::string& hiRom);
        virtual ~D1541();

        // State Management
        void saveState(StateWriter& wrtr) const override;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr) override;

        // Reset function
        void reset() override;

        // Advance drive via tick method
        void tick(uint32_t cycles) override;

        // Get disk path
        std::string getCurrentDiskPath() const override { return isDiskLoaded() ? loadedDiskName : std::string{}; }

        // Compatibility check
        inline bool canMount(DiskFormat fmt) const override { return fmt == DiskFormat::D64; }

        // IRQ handling
        void updateIRQ() override;

        // Drive interface
        inline bool isTrack0() { return currentTrack == 0; }
        inline bool isDiskLoaded() const override { return diskLoaded; }
        inline const std::string& getLoadedDiskName() const override { return loadedDiskName; }
        inline uint8_t getCurrentTrack() const override { return currentTrack; }
        inline uint8_t getCurrentSector() const override { return currentSector; }
        inline bool getByteReadyLow() const { return d1541mem.getVIA2().mechHasBytePending(); }
        inline bool isWriteProtected() const { return diskWriteProtected; }
        void loadDisk(const std::string& path) override;
        void unloadDisk() override;

        // Emulator UI interface
        inline bool hasTrackSector() const override { return true; }
        inline int getTrack() const override { return int(uiTrack) + 1; }
        inline int getSector() const override { return uiSector; }

        inline const char* getDriveModelName() const override { return "1541"; }
        inline bool hasDiskInserted() const override { return isDiskLoaded(); }
        inline std::string getMountedImagePath() const override { return getCurrentDiskPath(); }

        void getDriveIndicators(std::vector<Indicator>& out) const override;

        // IECBUS communication
        void onListen() override;
        void onUnListen() override;
        void onTalk() override;
        void onUnTalk() override;
        void onSecondaryAddress(uint8_t sa) override;

        // Motor control
        inline void startMotor() override { motorOn = true;  }
        inline void stopMotor()  override { motorOn = false; }
        inline bool isMotorOn() const override { return motorOn; }

        // IEC
        inline bool isSRQAsserted() const override { return srqAsserted; }
        inline void setSRQAsserted(bool state) override { srqAsserted = state; }
        void atnChanged(bool atnLow) override;
        void clkChanged(bool clkLow)  override;
        void dataChanged(bool dataLow) override;

        // Getters
        inline DriveModel getDriveModel() const override { return DriveModel::D1541; }
        inline bool getAtnLineLow() const override { return atnLineLow; }
        inline bool getClkLineLow() const override { return clkLineLow; }
        inline bool getDataLineLow() const override { return dataLineLow; }
        inline bool getSRQAsserted() const override { return srqAsserted; }

        // Presence helpers
        inline bool hasDonePresenceAck() const { return presenceAckDone; }
        inline void markPresenceAckDone()      { presenceAckDone = true; }
        inline bool isIECSelected() const      { return iecListening || iecTalking; }

        // Drive Mechanics
        void onStepperPhaseChange(uint8_t oldPhase, uint8_t newPhase);
        void setDensityCode(uint8_t code);
        void onVIA2PortAWrite(uint8_t value, uint8_t ddrA);
        void acceptGCRWriteByte(uint8_t value);
        void tryDecodeWrittenGCR();

        // Raw GCR write mechanics
        void setDiskWriteGate(bool enabled);
        void onVIA2PortARead(uint8_t value);

        // Status tracking
        DriveError  lastError;
        DriveStatus status;

        // Atn Ack
        inline void setPresenceAckDone(bool done) { presenceAckDone = done; }
        inline bool getPresenceAckDone() const { return presenceAckDone; }

        // Helper to force the IEC BUS state
        void forceSyncIEC() override;

        // ML Monitor
        inline bool hasCIA()  const override { return false; }
        inline bool hasVIA1() const override { return true;  }
        inline bool hasVIA2() const override { return true;  }
        inline bool hasFDC()  const override { return false; }
        inline bool isDrive() const override { return true;  }
        inline const CPU* getDriveCPU() const override { return &driveCPU; }
        inline CPU* getDriveCPU() override { return &driveCPU; }
        inline const D1541Memory* getMemory() const override { return &d1541mem; }
        inline D1541Memory* getMemory() override { return &d1541mem; }
        inline const D1541VIA* getVIA1() const override { return &d1541mem.getVIA1(); }
        inline const D1541VIA* getVIA2() const override { return &d1541mem.getVIA2(); }
        inline DriveStatus getDriveStatus() const override { return status; }
        const char* getDriveTypeName() const noexcept override { return "1541"; }
        IECSnapshot snapshotIEC() const override;

    protected:
        bool motorOn;

    private:

        // Owning pointers
        D1541Memory d1541mem;
        CPU driveCPU;
        GCRCodec gcrCodec;
        IRQLine IRQ;

        // Floppy factory
        std::unique_ptr<Disk> diskImage;

        // Track sectors for UI
        std::vector<uint8_t> gcrSectorAtPos;

        // Floppy Image
        std::string loadedDiskName;
        bool        diskLoaded;
        bool        diskWriteProtected;

        // IECBUS
        bool atnLineLow;
        bool clkLineLow;
        bool dataLineLow;
        bool srqAsserted;
        bool iecLinesPrimed;
        bool iecListening;
        bool iecTalking;
        bool presenceAckDone;
        bool expectingSecAddr;
        bool expectingDataByte;
        uint8_t currentListenSA;
        uint8_t currentTalkSA;

        // IEC listener data RX (C64 -> drive, ATN high, drive listening)
        bool iecRxActive;
        int iecRxBitCount;
        uint8_t iecRxByte;

        // Drive geometry
        int     halfTrackPos;
        uint8_t currentTrack;
        uint8_t currentSector;
        uint8_t densityCode; // 0..3

        // GCR
        std::vector<uint8_t> gcrTrackStream;
        std::vector<uint8_t> gcrSync;
        int  gcrBitCounter; // Used to rate limit bits
        size_t gcrPos;
        bool gcrDirty;

        // Raw GCR track cache. Once written, the raw track is authoritative
        // until we explicitly flush/decode it back to the sector image.
        std::array<std::vector<uint8_t>, 35> rawGcrTrackCache;
        std::array<std::vector<uint8_t>, 35> rawGcrSyncCache;
        std::array<std::vector<uint8_t>, 35> rawGcrSectorCache;
        std::array<bool, 35> rawGcrTrackValid{};
        std::array<bool, 35> rawGcrTrackDirty{};

        std::vector<uint8_t> gcrWrittenMask;
        bool diskWriteGate;
        size_t pendingWritePos;
        bool pendingWritePosValid;
        bool trackModifiedByWrite;

        // UI Activity
        uint8_t uiTrack;
        uint8_t uiSector;
        bool uiLedWasOn;

        bool gcrTick();
        void gcrAdvance(uint32_t dc);
        void rebuildGCRTrackStream();
        void saveCurrentRawTrackToCache();
        void loadCurrentRawTrackFromCacheOrBuild();
        void invalidateRawGcrCache();

        void sampleHeaderAtCurrentPosition(size_t pos);
        size_t findHeaderPosForSector(uint8_t track, uint8_t sector) const;

#ifdef Debug
        void debugDumpDirectorySectors(const char* tag);
        void debugDumpWriteContext(const char* tag);
        void debugDumpGcrWindow(const char* tag, size_t center, int before, int after);
#endif

        std::vector<uint8_t> writeGcrBuffer;
        uint8_t lastHeaderTrack;
        uint8_t lastHeaderSector;
        bool haveLastHeader;
        size_t lastHeaderPos;
        bool lastHeaderValid;

        int readSyncRun;
        bool readAfterSync;
        std::vector<uint8_t> readGcrHeaderProbe;

        uint8_t lastRomHeaderTrack;
        uint8_t lastRomHeaderSector;
        bool lastRomHeaderValid;
        size_t lastRomHeaderPos;
        uint64_t lastRomHeaderCycle;

        int writeSyncRun;
        bool writeAfterSync;
        int writeGapRun;

        // Helpers
        inline int stepIndex(uint8_t p) const { return (p & 0x03) * 2; }
        int cyclesPerByteFromDensity(uint8_t code) const;
        void resetForMediaChange();
        void rebuildSyncMapForCurrentTrack();
        bool decodeRawSectorFromCurrentTrack(uint8_t track, uint8_t sector, std::vector<uint8_t>& outSector);
        void flushCurrentRawTrackToImage();
        void flushAllDirtyRawTracksToImage();
        void flushAndSaveDisk();

        // Debug
        bool debugVerifyRawSector(uint8_t track, uint8_t sector);
};

#endif // D1541_H
