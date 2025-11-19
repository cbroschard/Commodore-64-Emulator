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
        virtual FDC177x* getFDC() { return nullptr; }

        // Level Changed
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
        virtual std::vector<uint8_t> getDirectoryListing() = 0;
        virtual std::vector<uint8_t> loadFileByName(const std::string& name) = 0;

        enum class DriveError { NONE, NO_DISK, BAD_SECTOR, READ_ERROR, WRITE_ERROR } currentDriveError;
        enum class DriveStatus { IDLE, READY, READING, WRITING, SEEKING } currentDriveStatus;
        enum class DriveBusState { IDLE, AWAITING_COMMAND, TALKING, LISTENING } currentDriveBusState;

        // Timing simulation.
        virtual void tick() = 0; // Called each emulation cycle

        // Motor control.
        virtual void startMotor() = 0;
        virtual void stopMotor() = 0;
        virtual bool isMotorOn() const = 0;

    protected:

        std::unique_ptr<Disk> diskImage;

        // Non-owning pointers
        Logging* logger = nullptr;

        // Signal state
        bool expectingListen;
        bool expectingTalk;

        // Talking state
        uint8_t currentTalkByte;
        int talkBitPos;
        bool waitingForAck;
        int ackEdgeCountdown;
        bool prevClkLevel;
        bool ackHold;
        bool byteAckHold;
        bool haveListenCommand;
        bool haveSecondary;

        // Data receive state
        virtual void processListenBuffer();
        std::vector<uint8_t> listenBuffer;
        int ackDelay; // used to release data line after ACK

        // Talking state
        std::queue<uint8_t> talkQueue;

        void iecClkEdge(bool data, bool clk) override;

    private:

        // Serial receiver state
        uint8_t bitShiftRegister;
        int bitCount;
        bool lastClkHigh;
        int currentSecondaryAddress;

        // Helper
        void parseCommandByte(uint8_t byte);
};

#endif // DRIVE_H
