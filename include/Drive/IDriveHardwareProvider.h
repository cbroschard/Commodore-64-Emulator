// Copyright (c) 2025 Christopher Broschard
// All rights reserved.
//
// This source code is provided for personal, educational, and
// non-commercial use only. Redistribution, modification, or use
// of this code in whole or in part for any other purpose is
// strictly prohibited without the prior written consent of the author.

#ifndef IDRIVEHARDWAREPROVIDER_H
#define IDRIVEHARDWAREPROVIDER_H

#include "Drive/DriveHardware.h"
#include "Drive/DriveHardware.h"

class CPU;
class CPUBus;
class DriveCIABase;
class DriveFDCBase;
class DriveVIABase;
class MOS6530;
class MOS6532;

class IDriveHardwareProvider
{
public:
    virtual ~IDriveHardwareProvider() = default;

    virtual std::vector<DriveHardwareInfo> enumerateDriveHardware() const = 0;

    // CPU
    virtual CPU* getHardwareCPU(uint32_t id) { return nullptr; }
    virtual CPUBus* getHardwareBus(uint32_t id) { return nullptr; }

    virtual bool getDriveCPUContext(uint32_t cpuId, DriveCPUContext& context)
    {
        context = {};

        if (cpuId != 0)
            return false;

        context.cpuId = 0;
        context.busId = 1;

        context.cpu = getHardwareCPU(0);
        context.bus = getHardwareBus(1);

        return context.cpu != nullptr && context.bus != nullptr;
    }

    virtual bool debugStepCPU(uint32_t cpuId) { return false; }
    virtual DriveStepResult debugStepInstruction(uint32_t cpuId) { return DriveStepResult::Unsupported; }

    // CIA
    virtual DriveCIABase* getHardwareCIA(uint32_t id) { return nullptr; }

    // FDC
    virtual DriveFDCBase* getHardwareFDC(uint32_t id) { return nullptr; }

    // VIA
    virtual DriveVIABase* getHardwareVIA(uint32_t id) { return nullptr; }

    // MOS 6530
    virtual MOS6530* getHardwareRRIOT(uint32_t hardwareId) { return nullptr; }

    // MOS 6532
    virtual MOS6532* getHardwareRIOT(uint32_t hardwareId) { return nullptr; }

    // Retrieve a read-only snapshot of a floppy mechanism.
    virtual bool getHardwareMechanismDebug(uint32_t hardwareId, DriveMechanismDebugView& view) const { return false; }
};

#endif // IDRIVEHARDWAREPROVIDER_H
