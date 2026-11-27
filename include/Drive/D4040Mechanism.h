// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef D4040MECHANISM_H_INCLUDED
#define D4040MECHANISM_H_INCLUDED

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include "Floppy/DiskFactory.h"
#include "GCR/GCRCodec.h"
#include "GCR/GCRTrackStream.h"
#include "StateReader.h"
#include "StateWriter.h"

class D4040Mechanism
{
    public:
        D4040Mechanism();
        ~D4040Mechanism();

        // Save/load state
        void saveState(StateWriter& wrtr) const;
        bool loadState(StateReader& rdr);

        void reset();

        void updateStepperPhase(uint8_t newPhase);
        bool writeGCRByte(size_t position, uint8_t value,  bool writeSync = false);

        // GCR dirty-track management
        bool hasUncommittedGCR() const;
        void cacheActiveTrack();
        bool restoreCachedTrack(uint8_t trackNumber);
        void clearDirtyTrackCache();

        // Media management
        void insertDisk(std::unique_ptr<Disk> image, const std::string& path);
        void removeDisk();

        // Media information
        bool hasDisk() const;
        bool isWriteProtected() const;

        const std::string& getDiskPath() const;

        Disk* getDiskImage();
        const Disk* getDiskImage() const;

        // Motor control
        void setMotorCommand(bool enabled);
        bool getMotorCommand() const;
        bool isMotorRunning() const;

        // Head position
        uint8_t getTrack() const;
        uint8_t getSector() const;
        uint8_t getHalfTrack() const;
        uint8_t getStepperPhase() const;

        // Motor timing
        uint32_t getMotorSpinDownCycles() const;

        // Advance physical motor state.
        void advanceMotor(uint32_t cycles);

        // Disk signals
        bool isSyncDetected() const;
        void setSyncDetected(bool detected);

        bool isByteReady() const;
        void setByteReady(bool ready);

        // Data latches
        uint8_t getReadLatch() const;
        void setReadLatch(uint8_t value);

        uint8_t getWriteLatch() const;
        void setWriteLatch(uint8_t value);

        // GCR track access
        GCRTrackStream& getGCRTrack();
        const GCRTrackStream& getGCRTrack() const;

        // Active GCR stream state
        size_t getGCRPosition() const;
        void setGCRPosition(size_t position);

        uint8_t getLoadedGCRTrack() const;
        void setLoadedGCRTrack(uint8_t track);

        bool isGCRValid() const;
        void setGCRValid(bool valid);

        // GCR timing
        uint32_t getGCRCycleAccumulator() const;
        void setGCRCycleAccumulator(uint32_t cycles);

        // GCR modification state
        bool isGCRDirty() const;
        void setGCRDirty(bool dirty);

        // Write statistics
        uint32_t getGCRBytesWritten() const;
        uint32_t getGCRSyncBytesWritten() const;
        void resetGCRWriteStatistics();

        // GCR decoder
        void resetGCRDecoder();

        // Feed one encoded GCR byte into the decoder.
        // Returns true when a decoded byte is available.
        bool decodeGCRByte(uint8_t encodedByte, const GCRCodec& codec, uint8_t& decodedByte);

        const GCRTrackStream* findDirtyTrack(uint8_t track) const;

        const std::unordered_map<uint8_t, GCRTrackStream>& getDirtyTracks() const;

    private:
        // Per-mechanism GCR state
        GCRTrackStream gcrTrack;

        // Media
        std::unique_ptr<Disk> diskImage;

        bool diskInserted;
        std::string diskPath;
        bool writeProtected;

        bool motorCommandOn;
        bool motorOn;
        uint32_t motorSpinDownCycles;

        uint8_t track;
        uint8_t sector;
        uint8_t stepperPhase;

        // Physical head position: track number * 2.
        // Track 1 = 2, track 18 = 36, track 35 = 70.
        uint8_t halfTrack;

        // Whether we have observed an initial stepper phase.
        bool stepperInitialized;

        // Disk signals
        bool syncDetected;
        bool byteReady;

        // Data transfer
        uint8_t readLatch;
        uint8_t writeLatch;

        size_t gcrPos;

        // Track number currently represented by gcrTrack.
        uint8_t loadedGCRTrack;

        // Indicates whether the stream matches the mounted
        // disk and current physical head position.
        bool gcrValid;

        // Accumulated emulated cycles since the last GCR byte.
        uint32_t gcrCycleAccumulator;

        // In-memory GCR track has been modified.
        bool gcrDirty;

        // Number of GCR bytes recorded on the current track.
        uint32_t gcrBytesWritten;

        uint32_t gcrSyncBytesWritten;

        // GCR read decoder state.
        //
        // The physical track contains five GCR bytes for
        // every four decoded data bytes.
        //
        // The controller receives decoded bytes through VIA Port A.
        uint8_t gcrEncodedBuffer[5];
        uint8_t gcrEncodedCount;

        uint8_t gcrDecodedBuffer[4];
        uint8_t gcrDecodedIndex;

        // Only modified tracks are cached.
        // Key: physical whole-track number (1-35).
        std::unordered_map<uint8_t, GCRTrackStream> dirtyTracks;
};

#endif // D4040MECHANISM_H_INCLUDED
