// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef DRIVE_H
#define DRIVE_H

// Forward declarations
class FDC177x;

#include <queue>
#include <memory>
#include "cpu.h"
#include "Drive/DriveChips.h"
#include "Floppy/DiskFactory.h"
#include "Peripheral.h"
#include "Logging.h"

class Drive : public Peripheral
{
    public:
        Drive();
        virtual ~Drive();

        // Pointers
        inline void attachLoggingInstance(Logging* logger) { this->logger = logger; }

        virtual void driveControlClkLine(bool clkLow);
        virtual void driveControlDataLine(bool dataLow);

        // Level Changed (callbacks from IEC bus)
        void atnChanged(bool atnAsserted) override;
        void dataChanged(bool level) override;

        // Check the compatibility for the drive and floppy type
        virtual bool canMount(DiskFormat fmt) const = 0;

        // Returns true if the format is supported and loading succeeded.
        bool insert(const std::string &path);

        // Disk handling
        virtual bool isDiskLoaded() const = 0;
        virtual void loadDisk(const std::string& path) = 0;
        virtual void unloadDisk() = 0;

        // Getters
        virtual uint8_t getCurrentTrack() const = 0;
        virtual uint8_t getCurrentSector() const = 0;

        // IEC Bus
        virtual bool getAtnLineLow() const = 0;
        virtual bool getClkLineLow() const = 0;
        virtual bool getDataLineLow() const = 0;
        virtual bool getSRQAsserted() const = 0;
        virtual void forceSyncIEC() = 0;

        enum class DriveError { NONE, NO_DISK, BAD_SECTOR, READ_ERROR, WRITE_ERROR } currentDriveError;
        enum class DriveStatus { IDLE, READY, READING, WRITING, SEEKING } currentDriveStatus;
        enum class DriveBusState { IDLE, AWAITING_COMMAND, TALKING, LISTENING } currentDriveBusState;

        // Timing simulation.
        virtual void tick(uint32_t cycles) = 0; // Called each emulation cycle

        // Motor control.
        virtual void startMotor() = 0;
        virtual void stopMotor() = 0;
        virtual bool isMotorOn() const = 0;

        // ML Monitor
        virtual DriveStatus getDriveStatus() const = 0;
        bool isDrive() const override { return true; }
        virtual bool hasCIA() const = 0;
        virtual bool hasVIA1() const = 0;
        virtual bool hasVIA2() const = 0;
        virtual bool hasFDC() const = 0;
        Drive* asDrive() override { return this; }
        virtual const CPU* getDriveCPU() const = 0;
        virtual CPU* getDriveCPU() = 0;
        virtual const FDC177x* getFDC() const { return nullptr; }
        virtual DriveMemoryBase* getMemory() = 0;
        virtual const DriveMemoryBase* getMemory() const = 0;
        virtual const DriveVIABase* getVIA1() const = 0;
        virtual const DriveVIABase* getVIA2() const = 0;
        virtual const DriveCIABase* getCIA() const { return nullptr; }
        struct IECSnapshot
        {
            // Observed bus levels (as seen by the drive)
            bool atnLow;
            bool clkLow;
            bool dataLow;
            bool srqLow;

            // What the drive is actively pulling low (from Peripheral::assert*)
            bool drvAssertAtn;
            bool drvAssertClk;
            bool drvAssertData;
            bool drvAssertSrq;

            // Protocol mode
            DriveBusState busState;
            bool listening;
            bool talking;
            int secondaryAddress;

            // Legacy bit shift state (from Peripheral)
            uint8_t shiftReg;
            int bitsProcessed;

            // Handshake / talker state (from Drive)
            bool waitingForAck;
            int  ackEdgeCountdown;
            bool swallowPostHandshakeFalling;
            bool waitingForClkRelease;
            bool prevClkLevel;
            bool ackHold;
            bool byteAckHold;
            int  ackDelay;
            size_t talkQueueLen;
        };

        virtual IECSnapshot snapshotIEC() const;

    protected:

        std::unique_ptr<Disk> diskImage;

        // Non-owning pointers
        Logging* logger = nullptr;

        // Talking state
        int currentSecondaryAddress;
        bool waitingForAck;
        int ackEdgeCountdown;
        bool swallowPostHandshakeFalling;
        bool waitingForClkRelease;
        bool prevClkLevel;
        bool ackHold;
        bool byteAckHold;

        // Data receive state
        int ackDelay; // used to release data line after ACK (legacy use)

        // Talking state
        std::queue<uint8_t> talkQueue;

    private:

        // Serial receiver state (legacy bit-shift state)
        bool lastClkHigh;
};

#endif // DRIVE_H
