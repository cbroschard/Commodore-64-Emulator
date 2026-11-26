// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.
#ifndef D4040_H
#define D4040_H

#include <array>
#include <cstdint>
#include <vector>
#include "CPU.h"
#include "Drive/D4040Bus.h"
#include "Drive/D4040Mechanism.h"
#include "Drive/D4040Memory.h"
#include "Drive/Drive.h"
#include "Drive/IDriveIndicatorView.h"
#include "Drive/IDrivePositionView.h"
#include "Drive/IDriveUiView.h"
#include "GCR/GCRCodec.h"
#include "IEEE488/IEEE488Device.h"
#include "IRQLine.h"
#include "StateReader.h"
#include "StateWriter.h"

class D4040 : public Drive, public IDriveIndicatorView, public IDrivePositionView, public IDriveUiView
{
    public:
        D4040(int deviceNumber, const std::string& fdcROM, const std::string& loROM, const std::string& hiROM1, const std::string& hiROM2);
        ~D4040();

        void reset() override;

        // State Management
        void saveState(StateWriter& wrtr) const override;
        bool loadState(const StateReader::Chunk& chunk, StateReader& rdr) override;

        bool attachIEEE488Bus(IEEE488Bus* ieee488Bus);

        // Drive execution
        void tick(uint32_t cycles) override;
        void updateIRQ() override;

        // UI interface
        inline bool hasTrackSector() const override { return true; }
        inline int getTrack() const override { return getCurrentTrack(); }
        inline int getSector() const override { return getCurrentSector(); }
        inline const char* getDriveModelName() const override { return "Commodore 4040"; }
        inline bool hasDiskInserted() const override { return isDiskLoaded(0) || isDiskLoaded(1); }

        // Disk support
        inline bool canMount(DiskFormat fmt) const override { return fmt == DiskFormat::D64; }
        inline bool isDiskLoaded() const override { return isDiskLoaded(0) || isDiskLoaded(1); }

        // Position
        inline uint8_t getCurrentTrack() const override { return mechanisms[0].track; }
        inline uint8_t getCurrentSector() const override { return mechanisms[0].sector; }

        // Drive model
        inline DriveModel getDriveModel() const override { return DriveModel::D4040; }

        // Drive indicators
        void getDriveIndicators(std::vector<IDriveIndicatorView::Indicator>& out) const override;

        // Standard Drive interface: mechanism 0.
        void loadDisk(const std::string& path) override;
        void unloadDisk() override;

        // 4040-specific dual-mechanism interface.
        bool mountDisk(uint8_t unit, const std::string& path);
        void unloadDisk(uint8_t unit);

        bool isDiskLoaded(uint8_t unit) const;
        std::string getDiskPath(uint8_t unit) const;

        // Motor control
        void startMotor() override;
        void stopMotor() override;

        bool tryUnloadDisk(uint8_t unit);
        bool tryUnloadAllDisks();

        // Disk persistence
        void flushAndSaveDisk() override;

        uint8_t getSelectedMechanism() const;

         // Disk path
        inline std::string getCurrentDiskPath() const override { return mechanisms[0].diskPath; }
        inline std::string getMountedImagePath() const override { return mechanisms[0].diskPath; }
        inline const std::string& getLoadedDiskName() const override { return mechanisms[0].diskPath; }

        // Motor state
        inline bool isMotorOn() const override {  return mechanisms[0].motorOn || mechanisms[1].motorOn; }

        inline D4040Mechanism& getMechanism(uint8_t unit) { return mechanisms[unit & 0x01]; }
        inline const D4040Mechanism& getMechanism(uint8_t unit) const { return mechanisms[unit & 0x01]; }

        // ML Monitor
        inline bool isDrive() const override { return true;  }
        inline const CPU* getDriveCPU() const override { return &driveCPU; }
        inline CPU* getDriveCPU() override { return &driveCPU; }
        inline const D4040Memory* getMemory() const override { return &d4040mem; }
        inline D4040Memory* getMemory() override { return &d4040mem; }
        inline DriveStatus getDriveStatus() const override { return status; }
        inline const char* getDriveTypeName() const noexcept override { return "4040"; }
        inline CPUBus* getDriveBus() override { return &d4040Bus6502; }
        inline const CPUBus* getDriveBus() const override { return &d4040Bus6502; }

        // DriveHardware discovery
        std::vector<DriveHardwareInfo> enumerateDriveHardware() const override;

        // CPU and bus access
        CPU* getHardwareCPU(uint32_t id) override;
        CPUBus* getHardwareBus(uint32_t id) override;

        // VIA access
        DriveVIABase* getHardwareVIA(uint32_t id) override;

        // CPU execution context
        bool getDriveCPUContext(uint32_t cpuId, DriveCPUContext& context) override;

        // Monitor execution
        bool debugStepCPU(uint32_t cpuId) override;
        DriveStepResult debugStepInstruction(uint32_t cpuId) override;

    private:
        D4040Memory d4040mem;
        D4040Bus d4040Bus6502;
        D4040Bus d4040Bus6504;
        CPU driveCPU;
        CPU controllerCPU;
        GCRCodec gcrCodec;
        IEEE488Device ieee488Device;
        IRQLine driveIRQ;
        IRQLine controllerIRQ;

        // Status tracking
        DriveError  lastError;
        DriveStatus status;

        struct DecodedGCRSector
        {
            uint8_t track = 0;
            uint8_t sector = 0;
            std::array<uint8_t, 256> data{};
        };

        // 4040 has two drive mechanisms
        std::array<D4040Mechanism, 2> mechanisms;

        int64_t controllerCycleBalance;
        int64_t driveCycleBalance;
        uint64_t dosInstructionsExecuted;
        uint64_t controllerInstructionsExecuted;
        int32_t debugSkipBreakpointCPU;

        uint8_t currentTrack;
        uint8_t currentSector;

        // Helpers
        void onIEEE488BusChanged(const IEEE488Bus::BusState& oldState, const IEEE488Bus::BusState& newState);
        void updateMechanisms();
        bool flushMechanism(uint8_t unit);
        void updateControllerInputs();
        bool rebuildGCRTrack(uint8_t unit);
        void advanceGCR(uint32_t cycles);
        bool decodeGCRTrack(const GCRTrackStream& stream, uint8_t track, std::vector<DecodedGCRSector>& sectors);
        bool getModifiedGCRSectors(uint8_t unit, uint8_t track, std::vector<DecodedGCRSector>& modifiedSectors);
        bool collectModifiedGCRSectors(uint8_t unit, std::vector<DecodedGCRSector>& modifiedSectors);
};

#endif // D4040_H
